/*
 * 2012+ Copyright (c) Evgeniy Polyakov <zbr@ioremap.net>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "elliptics.h"
#include "elliptics/interface.h"

static int dnet_discover_loop = 1;
static int dnet_discover_ttl = 3;

static int dnet_discovery_add_v4(struct dnet_node *n, struct dnet_addr *addr, int s)
{
	int err;
	struct ip_mreq command;

	err = setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, &dnet_discover_loop, sizeof(dnet_discover_loop));
	if (err < 0) {
		err = -errno;
    		dnet_log_err(n, "unable to set loopback option");
		goto err_out_exit;
  	}

	err = setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &dnet_discover_ttl, sizeof(dnet_discover_ttl));
	if (err < 0) {
		err = -errno;
		dnet_log_err(n, "unable to set %d hop limit", dnet_discover_ttl);
		goto err_out_exit;
	}

	command.imr_multiaddr = ((struct sockaddr_in *)addr->addr)->sin_addr;
	command.imr_interface = ((struct sockaddr_in *)n->addr.addr)->sin_addr;

	err = setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, &command, sizeof(command));
	if (err < 0) {
		err = -errno;
		dnet_log_err(n, "can not add multicast membership: %s", inet_ntoa(command.imr_multiaddr));
		goto err_out_exit;
	}

err_out_exit:
	return err;
}

static int dnet_discovery_add_v6(struct dnet_node *n, struct dnet_addr *addr __unused, int s)
{
	int err;

	err = setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &dnet_discover_loop, sizeof(dnet_discover_loop));
	if (err < 0) {
		err = -errno;
    		dnet_log_err(n, "unable to set loopback option");
		goto err_out_exit;
  	}

	err = setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &dnet_discover_ttl, sizeof(dnet_discover_ttl));
	if (err < 0) {
		err = -errno;
		dnet_log_err(n, "unable to set %d hop limit", dnet_discover_ttl);
		goto err_out_exit;
	}

err_out_exit:
	return err;
}

int dnet_discovery_add(struct dnet_node *n, struct dnet_config *cfg)
{
	struct dnet_addr addr;
	int err = -EEXIST;
	int s;

	if (n->autodiscovery_socket != -1)
		goto err_out_exit;

	memset(&addr, 0, sizeof(struct dnet_addr));

	cfg->sock_type = SOCK_DGRAM;
	cfg->proto = IPPROTO_IP;
	if (cfg->family == AF_INET6)
		cfg->proto = IPPROTO_IPV6;
	addr.addr_len = sizeof(addr.addr);

	err = dnet_fill_addr(&addr, cfg->addr, cfg->port, cfg->family, cfg->sock_type, cfg->proto);
	if (err) {
		dnet_log(n, DNET_LOG_ERROR, "Failed to get address info for %s:%s, family: %d, err: %d: %s.\n",
				cfg->addr, cfg->port, cfg->family, err, strerror(-err));
		goto err_out_exit;
	}

	s = socket(cfg->family, cfg->sock_type, 0);
	if (s < 0) {
		err = -errno;
		dnet_log_err(n, "failed to create multicast socket");
		goto err_out_exit;
	}

	if (cfg->family == AF_INET6)
		err = dnet_discovery_add_v6(n, &addr, s);
	else
		err = dnet_discovery_add_v4(n, &addr, s);

	if (err)
		goto err_out_close;

	n->autodiscovery_socket = s;
	n->autodiscovery_addr = addr;
	return 0;

err_out_close:
	close(s);
err_out_exit:
	return err;
}

static int dnet_discovery_send(struct dnet_node *n)
{
	char buf[sizeof(struct dnet_cmd) + sizeof(struct dnet_auth) + sizeof(struct dnet_addr_attr)];
	struct dnet_cmd *cmd;
	struct dnet_addr_attr *addr;
	struct dnet_auth *auth;
	int err;

	memset(buf, 0, sizeof(buf));

	cmd = (struct dnet_cmd *)buf;
	addr = (struct dnet_addr_attr *)(cmd + 1);
	auth = (struct dnet_auth *)(addr + 1);

	cmd->id = n->id;
	cmd->size = sizeof(struct dnet_addr_attr) + sizeof(struct dnet_auth);
	dnet_convert_cmd(cmd);

	addr->sock_type = n->sock_type;
	addr->proto = n->proto;
	addr->family = n->family;
	addr->addr = n->addr;
	dnet_convert_addr_attr(addr);

	memcpy(auth->cookie, n->cookie, DNET_AUTH_COOKIE_SIZE);
	dnet_convert_auth(auth);

	err = sendto(n->autodiscovery_socket, buf, sizeof(buf), 0, (void *)&n->autodiscovery_addr, n->autodiscovery_addr.addr_len);
	if (err < 0) {
		err = -errno;
		dnet_log_err(n, "autodiscovery sent: %s - %.*s", dnet_server_convert_dnet_addr(&addr->addr),
			(int)sizeof(auth->cookie), auth->cookie);
	} else {
		dnet_log(n, DNET_LOG_NOTICE, "autodiscovery sent: %s - %.*s\n", dnet_server_convert_dnet_addr(&addr->addr),
			(int)sizeof(auth->cookie), auth->cookie);
	}

	return err;
}

static int dnet_discovery_add_state(struct dnet_node *n, struct dnet_addr_attr *addr)
{
	struct dnet_config cfg;

	memset(&cfg, 0, sizeof(struct dnet_config));

	dnet_server_convert_addr_raw((struct sockaddr *)&addr->addr, addr->addr.addr_len, cfg.addr, sizeof(cfg.addr));
	snprintf(cfg.port, sizeof(cfg.port), "%d", dnet_server_convert_port((struct sockaddr *)&addr->addr, addr->addr.addr_len));
	cfg.family = addr->family;
	cfg.sock_type = addr->sock_type;
	cfg.proto = addr->proto;

	return dnet_add_state(n, &cfg);
}

static int dnet_discovery_recv(struct dnet_node *n)
{
	char buf[sizeof(struct dnet_cmd) + sizeof(struct dnet_auth) + sizeof(struct dnet_addr_attr)];
	struct dnet_cmd *cmd;
	struct dnet_addr_attr *addr;
	struct dnet_auth *auth;
	int err;
	struct dnet_addr remote;
	socklen_t len = n->autodiscovery_addr.addr_len;

	remote = n->autodiscovery_addr;

	cmd = (struct dnet_cmd *)buf;
	addr = (struct dnet_addr_attr *)(cmd + 1);
	auth = (struct dnet_auth *)(addr + 1);

	while (1) {
		err = recvfrom(n->autodiscovery_socket, buf, sizeof(buf), MSG_DONTWAIT, (void *)&remote, &len);
		if (err != sizeof(buf))
			return -EAGAIN;

		dnet_convert_cmd(cmd);
		dnet_convert_addr_attr(addr);
		dnet_convert_auth(auth);

		dnet_log(n, DNET_LOG_NOTICE, "autodiscovery recv: %s - %.*s\n", dnet_server_convert_dnet_addr(&addr->addr),
				(int)sizeof(auth->cookie), auth->cookie);

		if (!memcmp(n->cookie, auth->cookie, DNET_AUTH_COOKIE_SIZE)) {
			dnet_discovery_add_state(n, addr);
		}
	}

	return 0;
}

int dnet_discovery(struct dnet_node *n)
{
	int err;

	if (n->autodiscovery_socket == -1)
		return -ENOTSUP;

	err = dnet_discovery_recv(n);

	if (n->flags & DNET_CFG_JOIN_NETWORK)
		err = dnet_discovery_send(n);

	return err;
}
