# cleantests 	- clean local and subdirectories' tests
# buildtests	- build all local and subdirectories' tests
# runtests	- run all local tests 
# testall	- clean, build and run local and subdirectories' tests

include ../../../../../build_gateway.inc
include ../../../../../makefile.inc

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
	./rwsplit.sh $(TESTLOG) test_transaction_routing1.sql 127.0.0.1 4006 maxuser maxpwd
	./rwsplit.sh $(TESTLOG) test_transaction_routing2.sql 127.0.0.1 4006 maxuser maxpwd
	./rwsplit.sh $(TESTLOG) test_transaction_routing3.sql 127.0.0.1 4006 maxuser maxpwd
	@echo ""                        >> $(TESTLOG)


pesce:
	@echo "fine"


	