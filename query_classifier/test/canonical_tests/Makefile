# cleantests 	- clean local and subdirectories' tests
# buildtests	- build all local and subdirectories' tests
# runtests	- run all local tests 
# testall	- clean, build and run local and subdirectories' tests

include ../../../build_gateway.inc
include ../../../makefile.inc
include ../../../test.inc

CC = gcc
CPP = g++

TESTPATH 		:= $(shell pwd)
TESTLOG			:= $(TESTPATH)/testqclass.log
QUERY_CLASSIFIER_PATH 	:= $(ROOT_PATH)/query_classifier
LOG_MANAGER_PATH 	:= $(ROOT_PATH)/log_manager
UTILS_PATH		:= $(ROOT_PATH)/utils
CORE_PATH		:= $(ROOT_PATH)/server/core
TESTAPP			= $(TESTPATH)/canonizer

LDFLAGS=-L$(QUERY_CLASSIFIER_PATH) \
	-L$(LOG_MANAGER_PATH) \
	-L$(EMBEDDED_LIB) \
	-Wl,-rpath,$(DEST)/lib \
	-Wl,-rpath,$(EMBEDDED_LIB) \
	-Wl,-rpath,$(LOG_MANAGER_PATH) \
	-Wl,-rpath,$(QUERY_CLASSIFIER_PATH)

LIBS=-lstdc++ -lpthread -lquery_classifier -lz -ldl -lssl -laio -lcrypt -lcrypto -lrt \
	-llog_manager $(UTILS_PATH)/skygw_utils.o $(CORE_PATH)/buffer.o $(CORE_PATH)/atomic.o $(CORE_PATH)/spinlock.o

CFLAGS=-g $(MYSQL_HEADERS) \
	-I$(QUERY_CLASSIFIER_PATH) \
	$(MYSQL_HEADERS) \
	-I$(ROOT_PATH)/server/include \
	-I$(UTILS_PATH)

EMBFLAGS=$(shell mysql_config --cflags --libmysqld-libs)


testall:
	$(MAKE) cleantests
	$(MAKE) buildtests
	$(MAKE) runtests

cleantests:
	- $(DEL) *.o 
	- $(DEL) *~
	- $(DEL) canonizer
	- $(DEL) aria_log*
	- $(DEL) ib*

buildtests: $(OBJS)
	cp $(ERRMSG)/errmsg.sys .
	$(CC) $(CFLAGS) $(LDFLAGS) $(EMBFLAGS) $(LIBS) canonizer.c -o $(TESTAPP)

runtests:
	@echo ""				>  $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo $(shell date)			>> $(TESTLOG)
	@echo "Canonical Query Tests"		>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo ""				>> $(TESTLOG)
	./canontest.sh $(TESTLOG) input.sql output.sql expected.sql
