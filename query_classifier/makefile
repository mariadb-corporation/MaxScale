include ../build_gateway.inc
include ../makefile.inc

CC = gcc
CPP = g++

SRCS 			:= query_classifier.cc
UTILS_PATH		:= $(ROOT_PATH)/utils
QUERY_CLASSIFIER_PATH 	:= $(ROOT_PATH)/query_classifier
LOG_MANAGER_PATH	:= $(ROOT_PATH)/log_manager
SERVER_INC_PATH		:= $(ROOT_PATH)/server/include
MODULE_INC_PATH		:= $(ROOT_PATH)/server/modules/include

makeall: clean all

clean:
	$(MAKE) -C $(UTILS_PATH) clean
	- $(DEL) query_classifier.o 
	- $(DEL) libquery_classifier.so
	- $(DEL) libquery_classifier.so.1.0.1 
	- $(DEL) *~
	- $(DEL) depend

all: utils lib

cleantests:
	$(MAKE) -C test cleantests

buildtests:
	$(MAKE) -C test DEBUG=Y DYNLIB=Y buildtests

runtests:
	$(MAKE) -C test runtests

testall: 
	$(MAKE) -C test testall
	
utils:
	$(MAKE) -C $(UTILS_PATH) clean all

lib: libcomp liblink

libcomp: 
	$(CPP) -c $(CFLAGS) \
	$(MYSQL_HEADERS) \
	-I$(LOG_MANAGER_PATH) \
	-I$(SERVER_INC_PATH) \
	-I$(MODULE_INC_PATH) \
	-I$(UTILS_PATH) \
	-I./ \
	-fPIC ./query_classifier.cc -o query_classifier.o

liblink:
	 $(CPP) -shared \
	-L$(EMBEDDED_LIB) \
	-Wl,-soname,libquery_classifier.so \
	-Wl,-rpath,$(DEST)/lib \
	-Wl,-rpath,$(EMBEDDED_LIB) \
	-o libquery_classifier.so.1.0.1 ./query_classifier.o \
	$(LDLIBS) $(LDMYSQL) $(CPP_LDLIBS)
	$(DEL) ./libquery_classifier.so
	$(LINK) ./libquery_classifier.so.1.0.1 ./libquery_classifier.so


install: liblink
	install ./libquery_classifier.so.1.0.1 ./libquery_classifier.so $(DEST)/lib

depend:
	@$(DEL) depend
	$(CPP) -M $(CFLAGS) \
	$(MYSQL_HEADERS) \
	-I$(LOG_MANAGER_PATH) \
	-I$(SERVER_INC_PATH) \
	-I$(MODULE_INC_PATH) \
	-I$(UTILS_PATH) \
	-I./ \
	$(SRCS) > depend

include depend
