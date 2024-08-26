add_library(mirror SHARED mirror.cc mirrorsession.cc mirrorbackend.cc exporter.cc config.cc)
target_link_libraries(mirror maxscale-common ${RDKAFKA_LIBRARIES})
set_target_properties(mirror PROPERTIES VERSION "1.0.0"  LINK_FLAGS -Wl,-z,defs)
install_module(mirror core)
