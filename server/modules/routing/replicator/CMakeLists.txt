add_library(replicator SHARED rpl.cc replicator.cc sql.cc tokenizer.cc)
set_target_properties(replicator PROPERTIES VERSION "1.0.0"  LINK_FLAGS -Wl,-z,defs)
target_link_libraries(replicator maxscale-common)
install_module(replicator core)
