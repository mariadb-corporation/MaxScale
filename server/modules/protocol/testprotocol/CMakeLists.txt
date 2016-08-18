add_library(testprotocol SHARED testprotocol.c)
set_target_properties(testprotocol PROPERTIES VERSION "1.0.0")
install_module(testprotocol core)
