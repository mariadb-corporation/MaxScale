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
    DEL    := rm -fr
    LINK   := ln -s
    COPY   := cp
    NOHUP  := nohup
#endif

LIB := libmysqld.a

ifdef DYNLIB
     LIB := libmysqld.so.18
endif

# -O2 -g -pipe -Wformat -Werror=format-security -Wp,-D_FORTIFY_SOURCE=2 -fstack-protector --param=ssp-buffer-size=4 -fPIC

CFLAGS      := $(CFLAGS) -Wall
LDLIBS      := $(LDLIBS) -pthread -lm
LDMYSQL     := -lmysqld
CPP_LDLIBS  := -lstdc++

#
# Compiler flags, httpd arguments and debugger options
#
ifdef DEBUG
DEBUG_FLAGS := -DSS_DEBUG -pipe -Wformat -Werror=format-security -fstack-protector --param=ssp-buffer-size=4 -fPIC
      CFLAGS      := $(CFLAGS) -ggdb -O0 -pthread $(DEBUG_FLAGS)
endif


ifdef PROF
	CFLAGS := $(CFLAGS) -DSS_PROF
endif

ifeq "$(PROFILE)" "Y"
	CFLAGS += -pg
	LDFLAGS += -pg
endif

ifeq "$(GCOV)" "Y"
	CFLAGS += -fprofile-arcs -ftest-coverage
	LIBS += -lgcov
endif
