#!/usr/bin/python
# -*- coding: utf-8 -*-

from libelliptics_python import *

log = elliptics_log_file("/dev/stderr", 8)
n = elliptics_node_python(log)

group = 2

n.add_groups([group])
n.add_remote("localhost", 1025)

r = elliptics_range()
r.start = [0, 0, 0, 0]
r.end = [0xff, 0xdd]
r.group_id = group
r.limit_start = 1
r.limit_num = 2

ret = n.read_data_range(r)
print "len: ", len(ret)
print ret
