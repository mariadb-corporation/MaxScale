# cleantests 	- clean local and subdirectories' tests
# buildtests	- build all local and subdirectories' tests
# runtests	- run all local tests 
# testall	- clean, build and run local and subdirectories' tests

include ../../build_gateway.inc
include ../../makefile.inc

CC=cc
TESTLOG := $(shell pwd)/testutils.log

testall:
	$(MAKE) cleantests
	$(MAKE) buildtests
	$(MAKE) runtests
	@echo "No subdirectories to test"	>> $(TESTLOG)

cleantests:
	$(DEL) *~

buildtests:
	@echo "Nothing to build here so far"	>> $(TESTLOG)

runtests:
	@echo ""			>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo $(shell date)			>> $(TESTLOG)
	@echo "Test Utils"		>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo "Nothing to run here so far"	>> $(TESTLOG)
