/*
 * 2008+ Copyright (c) Evgeniy Polyakov <zbr@ioremap.net>
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

#include <netdb.h>
#include <boost/python.hpp>
#include <boost/python/list.hpp>
#include <boost/python/dict.hpp>

#include <elliptics/cppdef.h>

using namespace boost::python;
using namespace ioremap::elliptics;

enum elliptics_cflags {
	cflags_default = 0,
	cflags_direct = DNET_FLAGS_DIRECT,
	cflags_nolock = DNET_FLAGS_NOLOCK,
};

enum elliptics_ioflags {
	ioflags_default = 0,
	ioflags_append = DNET_IO_FLAGS_APPEND,
	ioflags_compress = DNET_IO_FLAGS_COMPRESS,
	ioflags_meta = DNET_IO_FLAGS_META,
	ioflags_prepare = DNET_IO_FLAGS_PREPARE,
	ioflags_commit = DNET_IO_FLAGS_COMMIT,
	ioflags_overwrite = DNET_IO_FLAGS_OVERWRITE,
	ioflags_nocsum = DNET_IO_FLAGS_NOCSUM,
	ioflags_plain_write = DNET_IO_FLAGS_PLAIN_WRITE,
	ioflags_cache = DNET_IO_FLAGS_CACHE,
	ioflags_cache_only = DNET_IO_FLAGS_CACHE_ONLY,
	ioflags_cache_remove_from_disk = DNET_IO_FLAGS_CACHE_REMOVE_FROM_DISK,
};

enum elliptics_log_level {
	log_level_data = DNET_LOG_DATA,
	log_level_error = DNET_LOG_ERROR,
	log_level_info = DNET_LOG_INFO,
	log_level_notice = DNET_LOG_NOTICE,
	log_level_debug = DNET_LOG_DEBUG,
};

static void elliptics_extract_arr(const list &l, unsigned char *dst, int *dlen)
{
	int length = len(l);

	if (length > *dlen)
		length = *dlen;

	memset(dst, 0, *dlen);
	for (int i = 0; i < length; ++i)
		dst[i] = extract<unsigned char>(l[i]);
}

struct elliptics_id {
	elliptics_id() : group_id(0), type(0) {}
	elliptics_id(list id_, int group_, int type_) : id(id_), group_id(group_), type(type_) {}

	elliptics_id(struct dnet_id &dnet) {
		for (unsigned int i = 0; i < sizeof(dnet.id); ++i)
			id.append(dnet.id[i]);

		group_id = dnet.group_id;
		type = dnet.type;
	}

	struct dnet_id to_dnet() const {
		struct dnet_id dnet;
		int len = sizeof(dnet.id);

 		elliptics_extract_arr(id, dnet.id, &len);

		dnet.group_id = group_id;
		dnet.type = type;

		return dnet;
	}

	list		id;
	uint32_t	group_id;
	int		type;
};

struct elliptics_range {
	elliptics_range() : offset(0), size(0),
		limit_start(0), limit_num(0), cflags(0), ioflags(0), group_id(0), type(0) {}

	list		start, end;
	uint64_t	offset, size;
	uint64_t	limit_start, limit_num;
	uint64_t	cflags;
	uint32_t	ioflags;
	int		group_id;
	int		type;
};

static void elliptics_extract_range(const struct elliptics_range &r, struct dnet_io_attr &io)
{
	int len = sizeof(io.id);

	elliptics_extract_arr(r.start, io.id, &len);
	elliptics_extract_arr(r.end, io.parent, &len);

	io.flags = r.ioflags;
	io.size = r.size;
	io.offset = r.offset;
	io.start = r.limit_start;
	io.num = r.limit_num;
	io.type = r.type;
}

class elliptics_log_wrap : public logger, public wrapper<logger> {
	public:
		elliptics_log_wrap(const int level = DNET_LOG_INFO) : logger(level) {};

		void log(const int level, const char *msg) {
			this->get_override("log")(level, msg);
		}

		unsigned long clone(void) {
			return this->get_override("clone")();
		}
};

class elliptics_log_file_wrap : public log_file, public wrapper<log_file> {
	public:
		elliptics_log_file_wrap(const char *file, const int level = DNET_LOG_INFO) :
			log_file(file, level) {};

		void log(const int level, const char *msg) {
			if (override log = this->get_override("log")) {
				log_file::log(level, msg);
				return;
			}

			log_file::log(level, msg);
		}

		void default_log(const int level, const char *msg) { this->log(level, msg); }

		unsigned long clone(void) {
			if (override clone = this->get_override("clone"))
				return log_file::clone();

			return log_file::clone();
		}

		unsigned long default_clone(void) { return this->clone(); }
};

class elliptics_config {
	public:
		elliptics_config() {
			memset(&config, 0, sizeof(struct dnet_config));
		}

		std::string cookie_get(void) const {
			std::string ret;
			ret.assign(config.cookie, sizeof(config.cookie));
			return ret;
		}

		void cookie_set(const std::string &cookie) {
			size_t sz = sizeof(config.cookie);
			if (cookie.size() + 1 < sz)
				sz = cookie.size() + 1;
			memset(config.cookie, 0, sizeof(config.cookie));
			snprintf(config.cookie, sz, "%s", (char *)cookie.data());
		}

		struct dnet_config		config;
};

class elliptics_node_python : public node, public wrapper<node> {
	public:
		elliptics_node_python(logger &l) : node(l) {}

		elliptics_node_python(logger &l, elliptics_config &cfg) : node(l, cfg.config) {}

		elliptics_node_python(const node &n): node(n) {}


};

class elliptics_session: public session, public wrapper<session> {
	public:
		elliptics_session(node &n) : session(n) {}

		elliptics_session(const session &s): session(s) {}

		void add_groups(const list &pgroups) {
			std::vector<int> groups;

			for (int i=0; i<len(pgroups); ++i)
				groups.push_back(extract<int>(pgroups[i]));

			session::add_groups(groups);
		}

		boost::python::list get_groups() {
			std::vector<int> groups = session::get_groups();
			boost::python::list res;
			for(size_t i=0; i<groups.size(); i++) {
				res.append(groups[i]);
			}

			return res;
		}

		void write_metadata_by_id(const struct elliptics_id &id, const std::string &remote, const list &pgroups, uint64_t cflags) {
			struct timespec ts;
			memset(&ts, 0, sizeof(ts));

			struct dnet_id raw = id.to_dnet();

			std::vector<int> groups;

			for (int i=0; i<len(pgroups); ++i)
				groups.push_back(extract<int>(pgroups[i]));

			write_metadata((const dnet_id&)raw, remote, groups, ts, cflags);
		}

		void write_metadata_by_data_transform(const std::string &remote, uint64_t cflags) {
			struct timespec ts;
			memset(&ts, 0, sizeof(ts));

			struct dnet_id raw;

			transform(remote, raw);

			write_metadata((const dnet_id&)raw, remote, groups, ts, cflags);
		}

		void read_file_by_id(struct elliptics_id &id, const std::string &file, uint64_t offset, uint64_t size) {
			struct dnet_id raw = id.to_dnet();
			read_file(raw, file, offset, size);
		}

		void read_file_by_data_transform(const std::string &remote, const std::string &file,
				uint64_t offset, uint64_t size,	int type) {
			read_file(remote, file, offset, size, type);
		}

		void write_file_by_id(struct elliptics_id &id, const std::string &file,
				uint64_t local_offset, uint64_t offset, uint64_t size,
				uint64_t cflags, unsigned int ioflags) {
			struct dnet_id raw = id.to_dnet();
			write_file(raw, file, local_offset, offset, size, cflags, ioflags);
		}

		void write_file_by_data_transform(const std::string &remote, const std::string &file,
				uint64_t local_offset, uint64_t offset, uint64_t size,
				uint64_t cflags, unsigned int ioflags, int type) {
			write_file(remote, file, local_offset, offset, size, cflags, ioflags, type);
		}

		std::string read_data_by_id(const struct elliptics_id &id, uint64_t offset, uint64_t size,
				uint64_t cflags, unsigned int ioflags) {
			struct dnet_id raw = id.to_dnet();
			return read_data_wait(raw, offset, size, cflags, ioflags);
		}

		std::string read_data_by_data_transform(const std::string &remote, uint64_t offset, uint64_t size,
				uint64_t cflags, unsigned int ioflags, int type) {
			return read_data_wait(remote, offset, size, cflags, ioflags, type);
		}

		list prepare_latest_by_id(const struct elliptics_id &id, uint64_t cflags, list gl) {
			struct dnet_id raw = id.to_dnet();

			std::vector<int> groups;
			for (int i = 0; i < len(gl); ++i)
				groups.push_back(extract<int>(gl[i]));

			prepare_latest(raw, cflags, groups);

			list l;
			for (unsigned i = 0; i < groups.size(); ++i)
				l.append(groups[i]);

			return l;
		}

		std::string prepare_latest_by_id_str(const struct elliptics_id &id, uint64_t cflags, list gl) {
			struct dnet_id raw = id.to_dnet();

			std::vector<int> groups;
			for (int i = 0; i < len(gl); ++i)
				groups.push_back(extract<int>(gl[i]));

			prepare_latest(raw, cflags, groups);

			std::string ret;
			ret.assign((char *)groups.data(), groups.size() * 4);

			return ret;
		}

		std::string read_latest_by_id(const struct elliptics_id &id, uint64_t offset, uint64_t size,
				uint64_t cflags, unsigned int ioflags) {
			struct dnet_id raw = id.to_dnet();
			return read_latest(raw, offset, size, cflags, ioflags);
		}

		std::string read_latest_by_data_transform(const std::string &remote, uint64_t offset, uint64_t size,
				uint64_t cflags, unsigned int ioflags, int type) {
			return read_latest(remote, offset, size, cflags, ioflags, type);
		}

		std::string write_data_by_id(const struct elliptics_id &id, const std::string &data, uint64_t remote_offset,
				uint64_t cflags, unsigned int ioflags) {
			struct dnet_id raw = id.to_dnet();
			return write_data_wait(raw, data, remote_offset, cflags, ioflags);
		}

		std::string write_data_by_data_transform(const std::string &remote, const std::string &data, uint64_t remote_offset,
				uint64_t cflags, unsigned int ioflags, int type) {
			return write_data_wait(remote, data, remote_offset, cflags, ioflags, type);
		}

		std::string write_cache_by_id(const struct elliptics_id &id, const std::string &data,
				uint64_t cflags, unsigned int ioflags, long timeout) {
			struct dnet_id raw = id.to_dnet();
			raw.type = 0;
			return write_cache(raw, data, cflags, ioflags, timeout);
		}

		std::string write_cache_by_data_transform(const std::string &remote, const std::string &data,
				uint64_t cflags, unsigned int ioflags, long timeout) {
			return write_cache(remote, data, cflags, ioflags, timeout);
		}

		std::string lookup_addr_by_data_transform(const std::string &remote, const int group_id) {
			return lookup_addr(remote, group_id);
		}

		std::string lookup_addr_by_id(const struct elliptics_id &id) {
			struct dnet_id raw = id.to_dnet();

			return lookup_addr(raw);
		}

		boost::python::tuple parse_lookup(const std::string &lookup) {
			const void *data = lookup.data();

			struct dnet_addr *addr = (struct dnet_addr *)data;
			struct dnet_cmd *cmd = (struct dnet_cmd *)(addr + 1);
			struct dnet_addr_attr *a = (struct dnet_addr_attr *)(cmd + 1);
			struct dnet_file_info *info = (struct dnet_file_info *)(a + 1);
			dnet_convert_file_info(info);

			std::string address(dnet_server_convert_dnet_addr(addr));
			int port = dnet_server_convert_port((struct sockaddr *)a->addr.addr, a->addr.addr_len);

			return make_tuple(address, port, info->size);
		}

		boost::python::tuple lookup_by_data_transform(const std::string &remote) {
			return parse_lookup(lookup(remote));
		}

		boost::python::tuple lookup_by_id(const struct elliptics_id &id) {
			struct dnet_id raw = id.to_dnet();

			return parse_lookup(lookup(raw));
		}

		struct dnet_node_status update_status_by_id(const struct elliptics_id &id, struct dnet_node_status &status) {
			struct dnet_id raw = id.to_dnet();

			update_status(raw, &status);
			return status;
		}
		
		struct dnet_node_status update_status_by_string(const std::string &saddr, const int port, const int family,
									struct dnet_node_status &status) {
			update_status(saddr.c_str(), port, family, &status);
			return status;
		}

		boost::python::list read_data_range(const struct elliptics_range &r) {
			struct dnet_io_attr io;
			elliptics_extract_range(r, io);

			std::vector<std::string> ret;
			ret = session::read_data_range(io, r.group_id, r.cflags);

			boost::python::list l;

			for (size_t i = 0; i < ret.size(); ++i) {
				l.append(ret[i]);
			}

			return l;
		}

		boost::python::list get_routes() {

			std::vector<std::pair<struct dnet_id, struct dnet_addr> > routes;
			std::vector<std::pair<struct dnet_id, struct dnet_addr> >::iterator it;

			boost::python::list res;

			routes = session::get_routes();

			for (it = routes.begin(); it != routes.end(); it++) {
				struct elliptics_id id(it->first);
				std::string address(dnet_server_convert_dnet_addr(&(it->second)));

				res.append(make_tuple(id, address));
			}

			return res;
		}

		std::string exec_name(const struct elliptics_id &id, const std::string &event,
				const std::string &data, const std::string &binary) {
			struct dnet_id raw = id.to_dnet();

			return exec_locked(&raw, event, data, binary);
		}

		std::string exec_name_by_name(const std::string &remote, const std::string &event,
				const std::string &data, const std::string &binary) {
			struct dnet_id raw;
			transform(remote, raw);
			raw.type = 0;
			raw.group_id = 0;

			return exec_locked(&raw, event, data, binary);
		}

		std::string exec_name_all(const std::string &event, const std::string &data, const std::string &binary) {
			return exec_locked(NULL, event, data, binary);
		}

		void remove_by_id(const struct elliptics_id &id, uint64_t cflags, uint64_t ioflags) {
			struct dnet_id raw = id.to_dnet();

			remove_raw(raw, cflags, ioflags);
		}

		void remove_by_name(const std::string &remote, uint64_t cflags, uint64_t ioflags, int type) {
			remove_raw(remote, cflags, ioflags, type);
		}

		list bulk_read_by_name(const list &keys, uint64_t cflags = 0) {
			unsigned int length = len(keys);

			std::vector<std::string> k;
			k.resize(length);

			for (unsigned int i = 0; i < length; ++i)
				k[i] = extract<std::string>(keys[i]);

			std::vector<std::string> ret =  bulk_read(k, cflags);

			list py_ret;
			for (size_t i = 0; i < ret.size(); ++i) {
				py_ret.append(ret[i]);
			}

			return py_ret;
		}

		list stat_log() {
			list statistics;
			callback c;
			std::string ret;
			int err;
			int i;

			err = dnet_request_stat(m_session, NULL, DNET_CMD_STAT_COUNT, DNET_ATTR_CNTR_GLOBAL,
				callback::complete_callback, (void *)&c);
			if (err < 0) {
				std::ostringstream str;
				str << "Failed to request statistics: " << err;
				throw std::runtime_error(str.str());
			}

			ret = c.wait(err);

			const void *data = ret.data();
			int size = ret.size();

			while (size > 0) {
				dict node_stat, storage_commands, proxy_commands, counters;
				struct dnet_addr *addr = (struct dnet_addr *)data;
				struct dnet_cmd *cmd = (struct dnet_cmd *)(addr + 1);
				if (cmd->size <= sizeof(struct dnet_addr_stat)) {
					size -= cmd->size + sizeof(struct dnet_addr) + sizeof(struct dnet_cmd);
					data = (char *)data + cmd->size + sizeof(struct dnet_addr) + sizeof(struct dnet_cmd);
					continue;
				}

				struct dnet_addr_stat *as = (struct dnet_addr_stat *)(cmd + 1);

				dnet_convert_addr_stat(as, 0);
				std::string address(dnet_server_convert_dnet_addr(addr));
				node_stat[std::string("addr")] = address;
				node_stat[std::string("group_id")] = cmd->id.group_id;

				for (i = 0; i < as->num; ++i) {
					if (i < as->cmd_num) {
						storage_commands[std::string(dnet_counter_string(i, as->cmd_num))] = 
							make_tuple((unsigned long long)as->count[i].count, (unsigned long long)as->count[i].err);
					} else if (i < (as->cmd_num * 2)) {
						proxy_commands[std::string(dnet_counter_string(i, as->cmd_num))] = 
							make_tuple((unsigned long long)as->count[i].count, (unsigned long long)as->count[i].err);
					} else {
						counters[std::string(dnet_counter_string(i, as->cmd_num))] = 
							make_tuple((unsigned long long)as->count[i].count, (unsigned long long)as->count[i].err);
					}
				}

				node_stat["storage_commands"] = storage_commands;
				node_stat["proxy_commands"] = proxy_commands;
				node_stat["counters"] = counters;

				statistics.append(node_stat);

				int sz = sizeof(struct dnet_addr) + sizeof(struct dnet_cmd) + cmd->size;
				size -= sz;
				data = (char *)data + sz;
			}

			return statistics;
		}
};
BOOST_PYTHON_MEMBER_FUNCTION_OVERLOADS(add_remote_overloads, add_remote, 2, 3);

BOOST_PYTHON_MODULE(libelliptics_python) {
	class_<elliptics_id>("elliptics_id", init<>())
		.def(init<list, int, int>())
		.def_readwrite("id", &elliptics_id::id)
		.def_readwrite("group_id", &elliptics_id::group_id)
		.def_readwrite("type", &elliptics_id::type)
	;

	class_<elliptics_range>("elliptics_range", init<>())
		.def_readwrite("start", &elliptics_range::start)
		.def_readwrite("end", &elliptics_range::end)
		.def_readwrite("offset", &elliptics_range::offset)
		.def_readwrite("size", &elliptics_range::size)
		.def_readwrite("ioflags", &elliptics_range::ioflags)
		.def_readwrite("cflags", &elliptics_range::cflags)
		.def_readwrite("group_id", &elliptics_range::group_id)
		.def_readwrite("type", &elliptics_range::type)
		.def_readwrite("limit_start", &elliptics_range::limit_start)
		.def_readwrite("limit_num", &elliptics_range::limit_num)
	;

	class_<elliptics_log_wrap, boost::noncopyable>("elliptics_log", init<const uint32_t>())
		.def("log", pure_virtual(&logger::log))
		.def("clone", pure_virtual(&logger::clone))
	;

	class_<elliptics_log_file_wrap, boost::noncopyable, bases<logger> >("elliptics_log_file", init<const char *, const uint32_t>())
		.def("log", &log_file::log, &elliptics_log_file_wrap::default_log)
		.def("clone", &log_file::clone, &elliptics_log_file_wrap::default_clone)
	;

	class_<dnet_node_status>("dnet_node_status", init<>())
		.def_readwrite("nflags", &dnet_node_status::nflags)
		.def_readwrite("status_flags", &dnet_node_status::status_flags)
		.def_readwrite("log_level", &dnet_node_status::log_level)
	;

	class_<dnet_config>("dnet_config", init<>())
		.def_readwrite("wait_timeout", &dnet_config::wait_timeout)
		.def_readwrite("flags", &dnet_config::flags)
		.def_readwrite("check_timeout", &dnet_config::check_timeout)
		.def_readwrite("io_thread_num", &dnet_config::io_thread_num)
		.def_readwrite("nonblocking_io_thread_num", &dnet_config::nonblocking_io_thread_num)
		.def_readwrite("net_thread_num", &dnet_config::net_thread_num)
		.def_readwrite("client_prio", &dnet_config::client_prio)
	;
	
	class_<elliptics_config>("elliptics_config", init<>())
		.def_readwrite("config", &elliptics_config::config)
		.add_property("cookie", &elliptics_config::cookie_get, &elliptics_config::cookie_set)
	;

	class_<elliptics_node_python>("elliptics_node_python", init<logger &>())
		.def(init<logger &, elliptics_config &>())
		.def("add_remote", &node::add_remote, add_remote_overloads())
	;

	class_<elliptics_session>("elliptics_session", init<node &>())
		.def("add_groups", &elliptics_session::add_groups)
		.def("get_groups", &elliptics_session::get_groups)

		.def("read_file", &elliptics_session::read_file_by_id)
		.def("read_file", &elliptics_session::read_file_by_data_transform)
		.def("write_file", &elliptics_session::write_file_by_id)
		.def("write_file", &elliptics_session::write_file_by_data_transform)

		.def("read_data", &elliptics_session::read_data_by_id)
		.def("read_data", &elliptics_session::read_data_by_data_transform)

		.def("prepare_latest", &elliptics_session::prepare_latest_by_id)
		.def("prepare_latest_str", &elliptics_session::prepare_latest_by_id_str)

		.def("read_latest", &elliptics_session::read_latest_by_id)
		.def("read_latest", &elliptics_session::read_latest_by_data_transform)

		.def("write_data", &elliptics_session::write_data_by_id)
		.def("write_data", &elliptics_session::write_data_by_data_transform)

		.def("write_metadata", &elliptics_session::write_metadata_by_id)
		.def("write_metadata", &elliptics_session::write_metadata_by_data_transform)

		.def("write_cache", &elliptics_session::write_cache_by_id)
		.def("write_cache", &elliptics_session::write_cache_by_data_transform)

		.def("lookup_addr", &elliptics_session::lookup_addr_by_data_transform)
		.def("lookup_addr", &elliptics_session::lookup_addr_by_id)

		.def("lookup", &elliptics_session::lookup_by_data_transform)
		.def("lookup", &elliptics_session::lookup_by_id)

		.def("update_status", &elliptics_session::update_status_by_id)
		.def("update_status", &elliptics_session::update_status_by_string)

		.def("read_data_range", &elliptics_session::read_data_range)

		.def("get_routes", &elliptics_session::get_routes)
		.def("stat_log", &elliptics_session::stat_log)

		.def("exec_event", &elliptics_session::exec_name)
		.def("exec_event", &elliptics_session::exec_name_by_name)
		.def("exec_event", &elliptics_session::exec_name_all)

		.def("remove", &elliptics_session::remove_by_id)
		.def("remove", &elliptics_session::remove_by_name)

		.def("bulk_read", &elliptics_session::bulk_read_by_name)
	;

	enum_<elliptics_cflags>("command_flags")
		.value("default", cflags_default)
		.value("direct", cflags_direct)
		.value("nolock", cflags_nolock)
	;

	enum_<elliptics_ioflags>("io_flags")
		.value("default", ioflags_default)
		.value("append", ioflags_append)
		.value("compress", ioflags_compress)
		.value("meta", ioflags_meta)
		.value("prepare", ioflags_prepare)
		.value("commit", ioflags_commit)
		.value("overwrite", ioflags_overwrite)
		.value("nocsum", ioflags_nocsum)
		.value("plain_write", ioflags_plain_write)
		.value("nodata", ioflags_plain_write)
		.value("cache", ioflags_cache)
		.value("cache_only", ioflags_cache_only)
		.value("cache_remove_from_disk", ioflags_cache_remove_from_disk)
	;

	enum_<elliptics_log_level>("log_level")
		.value("data", log_level_data)
		.value("error", log_level_error)
		.value("info", log_level_info)
		.value("notice", log_level_notice)
		.value("debug", log_level_debug)
	;
};
