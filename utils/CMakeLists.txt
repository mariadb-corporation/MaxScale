add_library(utils skygw_utils.cc)
add_dependencies(utils log_manager)
target_link_libraries(utils stdc++ log_manager)
