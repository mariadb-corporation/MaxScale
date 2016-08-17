add_library(qc_dummy SHARED qc_dummy.cc)
set_target_properties(qc_dummy PROPERTIES VERSION "1.0.0")
set_target_properties(qc_dummy PROPERTIES LINK_FLAGS -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/qc_dummy.map)
install_module(qc_dummy core)
