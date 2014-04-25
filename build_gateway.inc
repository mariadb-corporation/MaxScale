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
ROOT_PATH := $(HOME)/src/bazaar/tmp/maxscale

INC_PATH  := $(HOME)/usr/include
#
# Set prefix to MySQL Resources
#
MYSQL_ROOT := $(INC_PATH)/mysql

#
# Set prefix of the path to development headers
#
MYSQL_HEADERS := -I$(INC_PATH) -I$(MYSQL_ROOT)/ -I$(MYSQL_ROOT)/private/ -I$(MYSQL_ROOT)/extra/

#
# Set DYNLIB=Y if you want to link MaxScale with dynamic embedded lib
#
DYNLIB :=

#
# Set path to Embedded MySQL Server
#
EMBEDDED_LIB := $(HOME)/usr/lib64
ifdef DYNLIB
  EMBEDDED_LIB := $(HOME)/usr/lib64/dynlib
endif
#
# Set path to MySQL errors file
# 
ERRMSG := $(HOME)/usr/share/mysql
