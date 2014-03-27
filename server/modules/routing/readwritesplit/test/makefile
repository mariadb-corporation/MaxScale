# cleantests 	- clean local and subdirectories' tests
# buildtests	- build all local and subdirectories' tests
# runtests	- run all local tests 
# testall	- clean, build and run local and subdirectories' tests

include ../../../../../build_gateway.inc
include $(ROOT_PATH)/makefile.inc
include $(ROOT_PATH)/test.inc

CC=cc
TESTLOG := $(shell pwd)/testrouters.log
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
	@echo ""				>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo $(shell date)			>> $(TESTLOG)
	@echo "Test MaxScale R/W Split"		>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	./rwsplit.sh $(TESTLOG) $(THOST) $(TPORT_RW) $(TMASTER_ID) $(TUSER) $(TPWD) 
	@echo ""                        >> $(TESTLOG)


pesce:
	@echo "fine"


	