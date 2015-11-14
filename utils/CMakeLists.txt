add_library(utils skygw_utils.cc ../server/core/atomic.c)
target_link_libraries(utils stdc++ ${PCRE2_LIBRARIES})
add_dependencies(utils pcre2)
