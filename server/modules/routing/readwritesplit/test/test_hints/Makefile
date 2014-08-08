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
	- $(DEL) *.sql
	- $(DEL) *.output
	- $(DEL) *.log

testall: 
	-$(MAKE) cleantests
	-$(MAKE) DEBUG=Y buildtests
	-$(MAKE) runtests

buildtests:


runtests:
	@echo ""				>>  $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo $(shell date)			>> $(TESTLOG)
	@echo "Test Read/Write split router - hint routing"	>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo "Running simple tests"	>> $(TESTLOG)
	@echo ""                        >> $(TESTLOG)	
	./rwsplit_hints.sh $(TESTLOG) $(THOST) $(TPORT_RW_HINT) $(TMASTER_ID) $(TUSER) $(TPWD) simple_tests
	@echo ""                        >> $(TESTLOG)
	@echo "Running syntax error tests"	>> $(TESTLOG)
	@echo ""                        >> $(TESTLOG)	
	./syntax_check.sh $(TESTLOG) $(THOST) $(TPORT_RW_HINT) $(TMASTER_ID) $(TUSER) $(TPWD) error_tests
	@echo ""                        >> $(TESTLOG)
	@echo "Running complex tests"	>> $(TESTLOG)
	@echo ""                        >> $(TESTLOG)	
	./rwsplit_hints.sh $(TESTLOG) $(THOST) $(TPORT_RW_HINT) $(TMASTER_ID) $(TUSER) $(TPWD) complex_tests
	@echo ""                        >> $(TESTLOG)
	@echo "Running stack tests"	>> $(TESTLOG)	
	@echo ""                        >> $(TESTLOG)	
	./rwsplit_hints.sh $(TESTLOG) $(THOST) $(TPORT_RW_HINT) $(TMASTER_ID) $(TUSER) $(TPWD) stack_tests
	@echo ""                        >> $(TESTLOG)
	@cat $(TESTLOG)			>> $(TEST_MAXSCALE_LOG)
