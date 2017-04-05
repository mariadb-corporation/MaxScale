add_definitions(-DINI_MAX_LINE=16384 -DINI_USE_STACK=0 -DINI_ALLOW_MULTILINE)
add_library(inih ini.c)
