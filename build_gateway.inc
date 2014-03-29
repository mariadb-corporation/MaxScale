# 
# This file includes all dynamically changing build-related
# variables. 
#
# Modify to match with your needs. Do not commit any private
# changes to this file!
#

#
# Use debug flags
# Values : <empty> | Y
#
DEBUG := 

#
# Set build env
# Values : <empty> | Y
#
UNIX := Y

# 
# Set MaxScale branch directory
# Example	:= $(HOME)/src/MaxScale
#
ROOT_PATH	:=

#
# Main directory for headers
# Example	:= $(HOME)/usr/include
#
INC_PATH	:=

#
# Set prefix to MySQL Resources
# Example  	:= $(INC_PATH)/mysql
#
MYSQL_ROOT	:= 

#
# Set prefix of the path to development headers
# Example	:= -I$(INC_PATH) -I$(MYSQL_ROOT)/ -I$(MYSQL_ROOT)/private/ -I$(MYSQL_ROOT)/extra/
#
MYSQL_HEADERS 	:=

#
# Set DYNLIB=Y if you want to link MaxScale with dynamic embedded lib
# Values : <empty> | Y
#
DYNLIB :=

#
# Set path to Embedded MySQL Server
# Example
# EMBEDDED_LIB	:= $(HOME)/usr/lib64
# ifdef DYNLIB
#  EMBEDDED_LIB	:= $(HOME)/usr/lib64/dynlib
# endif
#
EMBEDDED_LIB 	:=
ifdef DYNLIB
  EMBEDDED_LIB	:=
endif

#
# Set path to MySQL errors file
# Example	:= $(HOME)/usr/share/mysql 
#
ERRMSG 		:=
