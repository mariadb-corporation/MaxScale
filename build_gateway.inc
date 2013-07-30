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
 DEBUGGER_PATH := 
 DEBUGGER_BIN  :=
#
# Set build env
#
UNIX := 

# 
# Set directories for building
#
ROOT_PATH := 
SHARED_PATH := 

ifndef $(MARIADB_SRC_PATH)
       MARIADB_SRC_PATH := 
endif
