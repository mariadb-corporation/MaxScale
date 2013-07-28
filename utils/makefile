include ../build_gateway.inc
include ../makefile.inc

CC = gcc
CPP = g++

makeall: clean all

clean:
	- $(DEL) *.o
	- $(DEL) *.so
	- $(DEL) *.a
	- $(DEL) *~

all: 
	$(CPP) -c $(CFLAGS) \
	-fPIC skygw_utils.cc -o skygw_utils.o

