# cleantests 	- clean local and subdirectories' tests
# buildtests	- build all local and subdirectories' tests
# runtests	- run all local tests 
# testall	- clean, build and run local and subdirectories' tests

include ../../../../build_gateway.inc
include $(ROOT_PATH)/makefile.inc
include $(ROOT_PATH)/test.inc

CC=cc
TESTLOG := $(shell pwd)/testrouting.log
RET := -1

cleantests:
	- $(DEL) *.o 
	- $(DEL) *~


testall: 
	-$(MAKE) cleantests
	-$(MAKE) DEBUG=Y buildtests
	-$(MAKE) runtests
	@echo ""                                >> $(TESTLOG)
	@echo "-------------------------------" >> $(TESTLOG)
	@echo $(shell date)                     >> $(TESTLOG)
	@echo "Test Read/Write Split Router"    >> $(TESTLOG)
	$(MAKE) -C $(ROOT_PATH)/server/modules/routing/readwritesplit testall


buildtests:
	$(MAKE) -C $(ROOT_PATH)/server/modules/routing/readwritesplit buildtests


runtests:
	@echo ""				>  $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo $(shell date)			>> $(TESTLOG)
	@echo "Test routing"			>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo "Nothing to run here so far"      >> $(TESTLOG)
	@cat $(TESTLOG)				>> $(TEST_MAXSCALE_LOG)
