add_library(kafkacdc SHARED kafkacdc.cc)
set_target_properties(kafkacdc PROPERTIES VERSION "1.0.0"  LINK_FLAGS -Wl,-z,defs)
target_link_libraries(kafkacdc replicator maxscale-common ${RDKAFKA_LIBRARIES})
install_module(kafkacdc core)
