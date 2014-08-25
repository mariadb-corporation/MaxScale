# cleantests 	- clean local and subdirectories' tests
# buildtests	- build all local and subdirectories' tests
# runtests	- run all local tests 
# testall	- clean, build and run local and subdirectories' tests

include ../../build_gateway.inc
include ../../makefile.inc
include ../../test.inc

CC = gcc
CPP = g++

TESTPATH 		:= $(shell pwd)
TESTLOG			:= $(TESTPATH)/testqclass.log
QUERY_CLASSIFIER_PATH 	:= $(ROOT_PATH)/query_classifier
LOG_MANAGER_PATH 	:= $(ROOT_PATH)/log_manager
UTILS_PATH		:= $(ROOT_PATH)/utils
TESTAPP = $(TESTPATH)/testmain

testall:buildtests
	$(MAKE) -C canonical_tests testall
testalllaters: 
	$(MAKE) cleantests 
	$(MAKE) DEBUG=Y DYNLIB=Y buildtests 
	$(MAKE) runtests
	

cleantests:
	- $(DEL) testmain.o 
	- $(DEL) testmain
	- $(DEL) data
	- $(DEL) *~

buildtests:
	@echo ""
	@echo "*********************************************************"
	@echo "Query classifier test disabled for now. vraa 4.7.2014"
	@echo "*********************************************************"
	@echo ""
	
buildtestslaters:
	$(CC) $(CFLAGS)	 \
	-L$(QUERY_CLASSIFIER_PATH) \
	-L$(LOG_MANAGER_PATH) \
	-L$(EMBEDDED_LIB) \
	-Wl,-rpath,$(DEST)/lib \
	-Wl,-rpath,$(EMBEDDED_LIB) \
	-Wl,-rpath,$(LOG_MANAGER_PATH) \
	-Wl,-rpath,$(QUERY_CLASSIFIER_PATH) \
	-o testmain -DSS_DEBUG \
	$(MYSQL_HEADERS) \
	-I$(QUERY_CLASSIFIER_PATH) \
	-I./ \
	-I$(UTILS_PATH) \
	testmain.c \
	$(UTILS_PATH)/skygw_utils.o \
	-lquery_classifier -lz -ldl -lssl -laio -lcrypt -lrt \
	-llog_manager \
	$(LDLIBS) $(LDMYSQL) 

runtests:
	@echo ""				>  $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo $(shell date)			>> $(TESTLOG)
	@echo "Test Query Classifier"		>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo "Query Classifier NOT TESTED due deprecated tests. vraa 4.7.2014" >> $(TESTLOG)
	@cat $(TESTLOG)			>> $(TEST_MAXSCALE_LOG)
	
	
runtestslaters:
	@echo ""				>  $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@echo $(shell date)			>> $(TESTLOG)
	@echo "Test Query Classifier"		>> $(TESTLOG)
	@echo "-------------------------------"	>> $(TESTLOG)
	@ -$(LAUNCH_DEBUGGER) $(TESTAPP) $(BACKGR)	2>> $(TESTLOG)
ifeq ($?, 0)
	@echo "Query Classifier PASSED"	>> $(TESTLOG)
else
	@echo "Query Classifier FAILED"	>> $(TESTLOG)
endif
	@cat $(TESTLOG)			>> $(TEST_MAXSCALE_LOG)
