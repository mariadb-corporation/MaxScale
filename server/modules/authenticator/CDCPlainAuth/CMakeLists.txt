add_library(CDCPlainAuth SHARED cdc_plain_auth.c)
target_link_libraries(CDCPlainAuth maxscale-common)
set_target_properties(CDCPlainAuth PROPERTIES VERSION "1.0.0")
install_module(CDCPlainAuth core)
