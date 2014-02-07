# cleantests 	- clean local and subdirectories' tests
# buildtests	- build all local and subdirectories' tests
# runtests	- run all local tests 
# testall	- clean, build and run local and subdirectories' tests

include ../../../build_gateway.inc
include ../../../makefile.inc

CC=cc
TESTLOG := $(shell pwd)/testhash.log

cleantests:
	- $(DEL) *.o 
	- $(DEL) testhash
	- $(DEL) *~

testall: 
	$(MAKE) cleantests
	$(MAKE) DEBUG=Y buildtests
	$(MAKE) runtests

buildtests : 
	$(CC) $(CFLAGS) \
	-I$(ROOT_PATH)/server/include \
	-I$(ROOT_PATH)/utils \
	testhash.c ../hashtable.o ../atomic.o ../spinlock.o -o testhash \

runtests: 
	@echo ""				>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo $(shell date)			>> $(TESTLOG)
	@echo "Test MaxScale core"		>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@./testhash 0 1 			2>> $(TESTLOG)
	@./testhash 10 1			2>> $(TESTLOG)
	@./testhash 1000 10			2>> $(TESTLOG)
	@./testhash 10 0			2>> $(TESTLOG)
	@./testhash 1500 17			2>> $(TESTLOG)
	@./testhash 1 1				2>> $(TESTLOG)
	@./testhash 10000 133			2>> $(TESTLOG)
	@./testhash 1000 1000			2>> $(TESTLOG)
	@./testhash 1000 100000			2>> $(TESTLOG)
	@echo "MaxScale core PASSED"		>> $(TESTLOG)
	@echo ""				>> $(TESTLOG)
