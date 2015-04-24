add_library(inih ini.c)
target_compile_definitions(inih PUBLIC INI_MAX_LINE=1024)
