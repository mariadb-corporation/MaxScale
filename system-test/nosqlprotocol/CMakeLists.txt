# Copy files to binary directory
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/package.json ${CMAKE_CURRENT_BINARY_DIR}/package-lock.json
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/package.json ${CMAKE_CURRENT_SOURCE_DIR}/package-lock.json
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/package.json ${CMAKE_CURRENT_SOURCE_DIR}/package-lock.json .)
add_custom_target(copy_npm_files ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/package.json ${CMAKE_CURRENT_BINARY_DIR}/package-lock.json)

# Run npm install
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/node_modules
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/package.json
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND npm install)
add_custom_target(install_node_modules ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/node_modules)
add_dependencies(install_node_modules copy_npm_files)

# Copy test files to binary directory
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND cp -R ${CMAKE_CURRENT_SOURCE_DIR}/test .)
add_custom_target(copy_test_files ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/test)
add_dependencies(copy_test_files install_node_modules)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/compare
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/compare
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND cp -R ${CMAKE_CURRENT_SOURCE_DIR}/compare .)
add_custom_target(copy_compare_files ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/compare)
add_dependencies(copy_compare_files copy_test_files)

#
# Tests
#
add_test_script(nosqlprotocol nosqlprotocol.sh nosqlprotocol LABELS nosqlprotocol REPL_BACKEND)

include_directories(${BSON_CXX_INCLUDE_DIR})
include_directories(${MONGO_CXX_INCLUDE_DIR})

add_test_executable_ex(
  NAME nosqlprotocol_bootstrap_shared
  SOURCE nosqlprotocol_bootstrap_shared.cc
  CONFIG nosqlprotocol_bootstrap_shared.cnf nosqlprotocol_bootstrap_shared.cnf
  VMS repl_backend second_maxscale
  LABELS nosqlprotocol)
add_dependencies(nosqlprotocol_bootstrap_shared mongo-cxx-driver mongo-c-driver)

target_link_libraries(nosqlprotocol_bootstrap_shared
  ${MONGO_CXX_LIBRARIES}
  ${MONGO_C_LIBRARIES}
  ${BSON_CXX_LIBRARIES}
  ${BSON_C_LIBRARIES}
  uuid
  sasl2
  resolv
  icuuc
  )
