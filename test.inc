#
# This file includes parameters needed for running tests and may be included
# in makefiles in test directories if seen useful.
# 
# hostname or IP address of MaxScale's host, for example:
#
# THOST         := 127.0.0.1
#
THOST           := 

#
# port of read connection router module, for example:
# TPORT_RCONN   := 4008
#
TPORT_RCONN     :=
#
# port of read/write split router module, for example:
# TPORT_RW      := 4006
#
TPORT_RW        :=
#
# port of read/write split router module with hints, for example:
# TPORT_RW_HINT      := 4009
#
TPORT_RW_HINT        :=
#
# username of MaxScale user, for example:
# TUSER         := maxuser
#
TUSER           :=
#
# password of MaxScale user, for example:
# TPWD          := maxpwd
#
TPWD            :=
#
# master's server_id, for example:
# TMASTER_ID    := 2
#
TMASTER_ID      := 

#
# Global test log where all log is gathered
# TEST_MAXSCALE_LOG   := $(ROOT_PATH)/test/test_maxscale.log
#
TEST_MAXSCALE_LOG   := 
