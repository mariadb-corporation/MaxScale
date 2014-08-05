# cleantests 	- clean local and subdirectories' tests
# buildtests	- build all local and subdirectories' tests
# runtests	- run all local tests 
# testall	- clean, build and run local and subdirectories' tests

include ../../build_gateway.inc
include ../../makefile.inc
include ../../test.inc

TEST_ROOT	:= $(ROOT_PATH)/test
PARENT_DIR	:= $(ROOT_PATH)/server
CUR_DIR 	:= $(PARENT_DIR)/test
export MAXSCALE_HOME=$(CUR_DIR)

CC=cc
TESTLOG := $(CUR_DIR)/testserver.log

ifndef $(HAVE_SRV)
HAVE_SRV := N
endif

cleantests:
	- $(DEL) *.o 
	- $(DEL) *~
ifeq ($(HAVE_SRV), N)
	- $(DEL) Documentation
	- $(DEL) bin
	- $(DEL) lib
	- $(DEL) MaxScale/etc
	- $(DEL) MaxScale/modules
	- $(DEL) MaxScale/mysql
endif
	$(MAKE) -C $(PARENT_DIR)/core cleantests

testall:
	$(MAKE) HAVE_SRV=$(HAVE_SRV) cleantests
	$(MAKE) HAVE_SRV=$(HAVE_SRV) buildtests
	$(MAKE) runtests
	@echo ""				>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo $(shell date)			>> $(TESTLOG)
	@echo "Test Server Core"		>> $(TESTLOG)
	$(MAKE) -C $(ROOT_PATH)/server/core		testall
	@echo "Core PASSED"			>> $(TESTLOG)
	@echo ""				>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo $(shell date)			>> $(TESTLOG)
	@echo "Test Modules"			>> $(TESTLOG)
	$(MAKE) -C $(ROOT_PATH)/server/modules/routing	testall
	@killall maxscale
	@echo "Routing modules PASSED"		>> $(TESTLOG)


buildtests: 
ifeq ($(HAVE_SRV), Y)
	@echo "Test server already built"
else
	$(MAKE) -C $(ROOT_PATH) buildtestserver
endif

runtests: 
	@echo ""				>  $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo $(shell date)			>> $(TESTLOG)
	@echo "Test MaxScale server"		>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo "MAXSCALE_HOME="$(MAXSCALE_HOME) >> $(TESTLOG)
	cp $(CUR_DIR)/MaxScale_test.cnf $(MAXSCALE_HOME)/etc/MaxScale.cnf
	bin/maxscale			2>> $(TESTLOG)
	@echo "Put your tests here"
	@sleep 5
	@echo "MaxScale server PASSED"	>> $(TESTLOG)
	@echo ""			>> $(TESTLOG)
	@cat $(TESTLOG)			>> $(TEST_MAXSCALE_LOG)
