# NOTE: This is currently not used. log_manager.cc is built directly into maxscale-common.
#       To be removed completely.
add_library(utils skygw_utils.cc ../server/core/atomic.c)
target_link_libraries(utils stdc++)
add_dependencies(utils pcre2)
