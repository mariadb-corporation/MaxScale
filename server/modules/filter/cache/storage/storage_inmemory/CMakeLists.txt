add_library(storage_inmemory SHARED
    inmemorystorage.cc
    inmemorystoragest.cc
    inmemorystoragemt.cc
    storage_inmemory.cc
    )
target_link_libraries(storage_inmemory cache maxscale-common)
set_target_properties(storage_inmemory PROPERTIES VERSION "1.0.0")
set_target_properties(storage_inmemory PROPERTIES LINK_FLAGS -Wl,-z,defs)
install_module(storage_inmemory core)
