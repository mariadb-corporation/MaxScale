include ../../../build_gateway.inc
include ../../../makefile.inc

CC=cc

clean:
	- $(DEL) *.o 
	- $(DEL) testhash
	- $(DEL) *~

all: 
	$(MAKE) clean
	$(MAKE) buildall
	$(MAKE) runall

buildall : 
	$(CC) $(CFLAGS) \
	-I$(ROOT_PATH)/server/include \
	-I$(ROOT_PATH)/utils \
	testhash.c ../hashtable.o ../atomic.o ../spinlock.o -o testhash \

runall:
	- ./testhash 0 1
	- ./testhash 10 1
	- ./testhash 1000 10
	- ./testhash 10 0
	- ./testhash 1500 17
	- ./testhash 1 1

