include ../build_gateway.inc
include ../makefile.inc

CC = gcc
CPP = g++

LOG_WRITER_PATH := $(shell pwd)

makeall: clean all

clean:
	make -C ../utils clean
	- $(DEL) *.o 
	- $(DEL) *.so
	- $(DEL) *.so.1.0.1 
	- $(DEL) *~

all: utils lib

utils:
	make -C $(ROOT_PATH)/utils clean all
	$(COPY) $(ROOT_PATH)/utils/skygw_utils.o ./

lib: libcomp liblink

libcomp: 
	$(CPP) -c $(CFLAGS) \
	-I$(MARIADB_SRC_PATH)/include/ \
	-I../utils/ -I./ \
	-fPIC ./log_writer.cc -o log_writer.o

liblink:
	 $(CPP) -shared \
	-Wl,-soname,liblog_writer.so \
	-o liblog_writer.so.1.0.1 log_writer.o \
	$(LDLIBS) $(CPP_LDLIBS)
	$(DEL) ./liblog_writer.so
	$(LINK) ./liblog_writer.so.1.0.1 ./liblog_writer.so
