# 
# This file includes all dynamically changing build-related
# variables. 
#
# Modify to match with your needs. Do not commit any private
# changes to this file!
#

#
# Set debug flags
#
 DEBUG         := 
 DEBUGGER      := 
 DEBUGGER_PATH := /usr/bin
 DEBUGGER_BIN  := ddd
#
# Set build env
#
UNIX := Y

# 
# Set directories for building
#
ROOT_PATH := /home/raatikka/src/bazaar/shared/skygateway
SHARED_PATH := /home/raatikka/src/bazaar/shared

ifndef $(MARIADB_SRC_PATH)
       MARIADB_SRC_PATH := $(SHARED_PATH)/maria/5.5
endif
