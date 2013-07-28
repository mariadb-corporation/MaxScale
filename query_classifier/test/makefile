include ../../build_gateway.inc
include ../../makefile.inc

CC = gcc
CPP = g++

TESTPATH := $(shell pwd)
QUERY_CLASSIFIER_PATH := $(ROOT_PATH)/query_classifier/
TESTAPP = $(TESTPATH)/testmain

runtest: makeall testall

makeall: clean all

clean:
	- $(DEL) testmain.o 
	- $(DEL) testmain
	- $(DEL) *~

all: testcomp testall

testcomp: 
	$(CC) $(CFLAGS)	 \
	-L$(QUERY_CLASSIFIER_PATH) \
	-L$(MARIADB_SRC_PATH)/libmysqld \
	-Wl,-rpath,$(DEST)/lib \
	-Wl,-rpath,$(MARIADB_SRC_PATH)/libmysqld \
	-Wl,-rpath,$(QUERY_CLASSIFIER_PATH)/ \
	-o testmain -DSS_DEBUG \
	-I$(MARIADB_SRC_PATH)/include testmain.c \
	-lquery_classifier $(LDLIBS) $(LDMYSQL) \
	$(QUERY_CLASSIFIER_PATH)/skygw_utils.o


testall: 
	- $(LAUNCH_DEBUGGER) $(TESTAPP) $(BACKGR)
