# cleantests 	- clean local and subdirectories' tests
# buildtests	- build all local and subdirectories' tests
# runtests	- run all local tests 
# testall	- clean, build and run local and subdirectories' tests

include ../../../build_gateway.inc
include ../../../makefile.inc
include ../../../test.inc

CC=cc
TESTLOG := $(shell pwd)/testcore.log

LOGPATH := $(ROOT_PATH)/log_manager
UTILSPATH := $(ROOT_PATH)/utils

LDFLAGS=-rdynamic -L$(LOGPATH) \
	-Wl,-rpath,$(DEST)/lib \
	-Wl,-rpath,$(LOGPATH) -Wl,-rpath,$(UTILSPATH) \
	-Wl,-rpath,$(EMBEDDED_LIB)

LIBS= -lz -lm -lcrypt -lcrypto -ldl -laio -lrt -pthread -llog_manager \
	-L../../inih/extra -linih -lssl -lstdc++ 

TESTS=testhash testspinlock testfilter testadminusers

cleantests:
	- $(DEL) *.o 
	- $(DEL) testhash
	- $(DEL) *~

testall: 
	$(MAKE) cleantests
	$(MAKE) DEBUG=Y buildtests
	$(MAKE) runtests

buildtests : $(TESTS)

testhash: testhash.c 
	$(CC) $(CFLAGS) \
	-I$(ROOT_PATH)/server/include \
	-I$(ROOT_PATH)/utils \
	testhash.c ../hashtable.o ../atomic.o ../spinlock.o -o testhash

testspinlock: testspinlock.c 
	$(CC) $(CFLAGS) \
	-I$(ROOT_PATH)/server/include \
	-I$(ROOT_PATH)/utils \
	testspinlock.c ../spinlock.o ../atomic.o ../thread.o -o testspinlock

testfilter: testfilter.c  libcore.a
	$(CC) $(CFLAGS) $(LDFLAGS) \
	-I$(ROOT_PATH)/server/include \
	-I$(ROOT_PATH)/utils \
	testfilter.c libcore.a $(UTILSPATH)/skygw_utils.o $(LIBS) -o testfilter

testadminusers: testadminusers.c  libcore.a
	$(CC) $(CFLAGS) $(LDFLAGS) \
	-I$(ROOT_PATH)/server/include \
	-I$(ROOT_PATH)/utils \
	testadminusers.c libcore.a $(UTILSPATH)/skygw_utils.o $(LIBS) -o testadminusers

libcore.a: ../*.o
	ar rv libcore.a ../*.o

runtests: $(TESTS)
	@echo ""				>  $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo $(shell date)			>> $(TESTLOG)
	@echo "Test MaxScale core"		>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	$(foreach var,$(TESTS),./runtest.sh $(var) $(TESTLOG);)
	@echo ""				>> $(TESTLOG)
	@cat $(TESTLOG)				>> $(TEST_MAXSCALE_LOG)
