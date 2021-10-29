add_compile_definitions(INI_MAX_LINE=16777216 INI_USE_STACK=0 INI_ALLOW_MULTILINE)
add_library(inih STATIC ini.c)
