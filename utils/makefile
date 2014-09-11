include ../build_gateway.inc
include ../makefile.inc

CC = gcc
CPP = g++
UTILS_PATH	:= $(ROOT_PATH)/utils

makeall: clean all

clean:
	- $(DEL) *.o
	- $(DEL) *.so
	- $(DEL) *.a
	- $(DEL) *~

all: 
	$(CPP) -c $(CFLAGS) -I$(UTILS_PATH) \
	-fPIC skygw_utils.cc -o skygw_utils.o

cleantests:
	$(MAKE) -C test cleantests

buildtests:
	$(MAKE) -C test DEBUG=Y buildtests

runtests:
	$(MAKE) -C test runtests

testall: 
	$(MAKE) -C test testall
