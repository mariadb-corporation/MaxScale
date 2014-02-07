# cleantests 	- clean local and subdirectories' tests
# buildtests	- build all local and subdirectories' tests
# runtests	- run all local tests 
# testall	- clean, build and run local and subdirectories' tests

include ../build_gateway.inc
include ../makefile.inc

export MAXSCALE_HOME=$(shell pwd)/MaxScale

CC=cc
TESTLOG := $(shell pwd)/testmaxscale.log

testall:
	$(MAKE) cleantests
	$(MAKE) buildtests
	$(MAKE) runtests
	@echo ""				>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo $(shell date)			>> $(TESTLOG)
	@echo "Test Log Manager"		>> $(TESTLOG)
	$(MAKE) -C $(ROOT_PATH)/log_manager			testall
	@echo "Log Manager PASSED"		>> $(TESTLOG)
	@echo ""				>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo $(shell date)			>> $(TESTLOG)
	@echo "Test Query Classifier"		>> $(TESTLOG)
	$(MAKE) -C $(ROOT_PATH)/query_classifier		testall
	@echo "Query Classifier PASSED"		>> $(TESTLOG)
	@echo ""				>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo $(shell date)			>> $(TESTLOG)
	@echo "Test MaxScale server"		>> $(TESTLOG)
	$(MAKE) -C $(ROOT_PATH)/server HAVE_SRV=$(HAVE_SRV)	testall
	@echo "MaxScale server PASSED"		>> $(TESTLOG)
	@echo ""				>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo $(shell date)			>> $(TESTLOG)
	@echo "Test utils"			>> $(TESTLOG)
	$(MAKE) -C $(ROOT_PATH)/utils				testall
	@echo "Utils PASSED"			>> $(TESTLOG)

cleantests:
	$(DEL) *~
	$(MAKE) -C $(ROOT_PATH)/log_manager		cleantests
	$(MAKE) -C $(ROOT_PATH)/query_classifier	cleantests
	$(MAKE) -C $(ROOT_PATH)/server			cleantests
	$(MAKE) -C $(ROOT_PATH)/utils			cleantests

buildtests:
	$(MAKE) -C $(ROOT_PATH) buildtestserver
	$(MAKE) -C $(ROOT_PATH)/log_manager 			buildtests
	$(MAKE) -C $(ROOT_PATH)/query_classifier		buildtests
	$(MAKE) -C $(ROOT_PATH)/server HAVE_SRV=$(HAVE_SRV)	buildtests
	$(MAKE) -C $(ROOT_PATH)/utils				buildtests

runtests:
	@echo ""			>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo $(shell date)			>> $(TESTLOG)
	@echo "Test MaxScale root"		>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo "Nothing to run here so far"	>> $(TESTLOG)
