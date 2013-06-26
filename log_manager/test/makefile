include ../../build_gateway.inc
include ../../makefile.inc

CC = gcc
CPP = g++

TESTPATH := $(shell pwd)
LOG_MANAGER_PATH := $(ROOT_PATH)/log_manager
TESTAPP = $(TESTPATH)/testlog

runtest: makeall testall

makeall: clean all

clean:
	- $(DEL) *.o 
	- $(DEL) testlog
	- $(DEL) *~

all: testcomp testall

testcomp:
	$(CC) $(CFLAGS)	 \
	-L$(LOG_MANAGER_PATH) \
	-Wl,-rpath,$(LOG_MANAGER_PATH)/ \
	-o testlog -DSS_DEBUG \
	-I$(SOLIDDB_SRC_PATH)/include \
	-I$(MARIADB_SRC_PATH)/include \
	-I$(LOG_MANAGER_PATH) -I$(ROOT_PATH)/utils testlog.c \
	-llog_manager $(LDLIBS) \
	$(LOG_MANAGER_PATH)/skygw_utils.o \

testall: 
	- $(LAUNCH_DEBUGGER) $(TESTAPP) $(BACKGR)
