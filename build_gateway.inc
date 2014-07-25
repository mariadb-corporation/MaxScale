#  
# This file includes all dynamically changing build-related
# variables. 
#
# Current values are to satisfy MaxScale build process.
#
# Modify to match with your needs. Do not commit any private
# changes to this file!
#
#

#
# Set debug flags
#
DEBUG := 

#
# Set build env
#
UNIX := Y

# 
# Set MaxScale branch directory
#
ROOT_PATH := $(HOME)/MaxScale_fork

INC_PATH  := /usr/include
#
# Set prefix to MySQL Resources
#
MYSQL_ROOT := $(HOME)/mdb/include/mysql

#
# Set prefix of the path to development headers
#
MYSQL_HEADERS := -I$(HOME)/mdb/include/ -I$(MYSQL_ROOT)/ -I$(MYSQL_ROOT)/extra -I$(MYSQL_ROOT)/private

#
# Set DYNLIB=Y if you want to link MaxScale with dynamic embedded lib
#
DYNLIB :=

#
# Set path to Embedded MySQL Server
#
EMBEDDED_LIB := $(HOME)/mdb/lib64
ifdef DYNLIB
  EMBEDDED_LIB := $(HOME)/usr/lib64/dynlib
endif
#
# Set path to MySQL errors file
# 
ERRMSG := $(HOME)/mdb/share/mysql/english
