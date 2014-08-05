# cleantests 	- clean local and subdirectories' tests
# buildtests	- build all local and subdirectories' tests
# runtests	- run all local tests 
# testall	- clean, build and run local and subdirectories' tests

include ../../../../../../build_gateway.inc
include $(ROOT_PATH)/makefile.inc
include $(ROOT_PATH)/test.inc

ARGS=6

CC=cc
TESTLOG := $(shell pwd)/testrwsplit_hints.log
RET := -1

cleantests:
	- $(DEL) *.o 
	- $(DEL) *~


testall: 
	-$(MAKE) cleantests
	-$(MAKE) DEBUG=Y buildtests
	-$(MAKE) runtests

buildtests:


runtests:
	@echo ""				>  $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo $(shell date)			>> $(TESTLOG)
	@echo "Test Read/Write split router - hint routing"	>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	./rwsplit_hints.sh $(TESTLOG) $(THOST) $(TPORT_RW) $(TMASTER_ID) $(TUSER) $(TPWD)
	@echo ""                        >> $(TESTLOG)
	@cat $(TESTLOG)			>> $(TEST_MAXSCALE_LOG)
