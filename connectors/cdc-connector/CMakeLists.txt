# Shared version of the library
add_library(cdc_connector SHARED cdc_connector.cpp)
target_link_libraries(cdc_connector ${JANSSON_LIBRARIES} crypto)
set_target_properties(cdc_connector PROPERTIES VERSION "1.0.0")

# Static version of the library
add_library(cdc_connector_static STATIC cdc_connector.cpp)
set_target_properties(cdc_connector_static PROPERTIES OUTPUT_NAME cdc_connector)

install_dev_library(cdc_connector devel)
install_dev_library(cdc_connector_static devel)
install_header(cdc_connector.h devel)
