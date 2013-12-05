#
# This file is static. It handles the dynamic content of build.inc, 
# for example, by setting compiler flags etc.

# Changes to environment, such as building directories etc. are made to 
# build_gateway.inc. 
# A template file build_gateway_l2x64.inc is stored in directory 'config'. 
# Template can't be used as such, but hopefully only little modifications are 
# needed, such as setting build directory etc.
#

#ifdef UNIX
    DEL    := rm -f
    LINK   := ln -s
    COPY   := cp
    NOHUP  := nohup
#endif

LIB := libmysqld.a

ifdef DYNLIB
     LIB := libmysqld.so.18
endif


CFLAGS      := $(CFLAGS) -Wall
LDLIBS      := $(LDLIBS) -pthread
LDMYSQL     := -lmysqld
CPP_LDLIBS  := -lstdc++

#
# Compiler flags, httpd arguments and debugger options
#
ifdef DEBUG
      DEBUG_FLAGS := -DSS_DEBUG
      CFLAGS      := $(CFLAGS) -ggdb -O0 -pthread $(DEBUG_FLAGS)
endif


ifdef PROF
	CFLAGS := $(CFLAGS) -DSS_PROF
endif