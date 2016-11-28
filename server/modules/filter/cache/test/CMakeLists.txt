add_executable(testrules testrules.cc ../rules.cc)
include_directories(..)
target_link_libraries(testrules maxscale-common jansson)

add_test(TestCache_rules testrules)
