include_directories(..)

add_executable(masking_testrules testrules.cc ../maskingrules.cc)
target_link_libraries(masking_testrules maxscale-common ${JANSSON_LIBRARIES})

add_test(test_masking_rules masking_testrules)
