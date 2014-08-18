# cleantests 	- clean local and subdirectories' tests
# buildtests	- build all local and subdirectories' tests
# runtests	- run all local tests 
# testall	- clean, build and run local and subdirectories' tests

include ../../../build_gateway.inc
include ../../../makefile.inc
include ../../../test.inc

CC=cc
TESTLOG := $(shell pwd)/testhash.log

TESTS=testhash testspinlock

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

runtests:
	@echo ""				>  $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo $(shell date)			>> $(TESTLOG)
	@echo "Test MaxScale core"		>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@ -./testhash 	 			2>> $(TESTLOG)
ifeq ($?,0)
	@echo "MaxScale hashtable PASSED"	>> $(TESTLOG)
else
	@echo "MaxScale hashtable FAILED"	>> $(TESTLOG)
endif
	@ -./testspinlock  			2>> $(TESTLOG)
ifeq ($?,0)
	@echo "MaxScale spinlock PASSED"	>> $(TESTLOG)
else
	@echo "MaxScale spinlock FAILED" 	>> $(TESTLOG)
endif
	@echo ""				>> $(TESTLOG)
	@cat $(TESTLOG)				>> $(TEST_MAXSCALE_LOG)
