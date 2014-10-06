add_executable(harness_ui harness_ui.c harness_common.c)
add_executable(harness harness_util.c harness_common.c)
target_link_libraries(harness_ui log_manager utils pthread)
target_link_libraries(harness log_manager utils pthread)