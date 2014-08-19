include ../build_gateway.inc
include ../makefile.inc

CC = gcc
CPP = g++

SRCS 		:= log_manager.cc
UTILS_PATH	:= $(ROOT_PATH)/utils
CUR_DIR 	:= $(shell pwd)

makeall: clean all

clean:
	$(MAKE) -C $(UTILS_PATH) clean
	- $(DEL) *.o 
	- $(DEL) *.so
	- $(DEL) *.so.1.0.1 
	- $(DEL) *~
	- $(DEL) depend

all: utils lib


utils:
	$(MAKE) -C $(UTILS_PATH) clean all

lib: libcomp liblink

libcomp: 
	$(CPP) -c $(CFLAGS) \
	$(MYSQL_HEADERS) \
	-I$(UTILS_PATH) -I./ \
	-fPIC ./log_manager.cc -o log_manager.o $(LDLIBS)

liblink:
	 $(CPP) -shared \
	-Wl,-soname,liblog_manager.so \
	-o liblog_manager.so.1.0.1 log_manager.o \
	$(LDLIBS) $(CPP_LDLIBS)
	$(DEL) ./liblog_manager.so
	$(LINK) ./liblog_manager.so.1.0.1 ./liblog_manager.so

install: liblink
	install liblog_manager.so.1.0.1 liblog_manager.so $(DEST)/lib

depend:
	@$(DEL) depend
	$(CPP) -M $(CFLAGS) \
	$(MYSQL_HEADERS) \
	-I$(UTILS_PATH) -I./ \
	$(SRCS) > depend

cleantests:
	$(MAKE) -C test cleantests

buildtests:
	$(MAKE) -C test DEBUG=Y buildtests

runtests:
	$(MAKE) -C test runtests

testall: 
	$(MAKE) -C test testall



include depend
