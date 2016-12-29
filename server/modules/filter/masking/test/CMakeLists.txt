include_directories(..)

add_executable(masking_testrules testrules.cc ../maskingrules.cc)
target_link_libraries(masking_testrules maxscale-common jansson)

add_test(TestMasking_rules masking_testrules)
