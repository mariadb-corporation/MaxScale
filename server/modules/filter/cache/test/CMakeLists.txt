add_executable(testrules testrules.c ../rules.c)
include_directories(..)
target_link_libraries(testrules maxscale-common jansson)

add_test(TestCache_rules testrules)
