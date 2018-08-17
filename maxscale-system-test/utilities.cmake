
# Helper function to add a configuration template
function(add_template name template)
  set(CNF_TEMPLATES "${CNF_TEMPLATES}{\"${name}\",\"${template}\"}," CACHE INTERNAL "")
endfunction()

# Default test timeout
set(TIMEOUT 900)

# This functions adds a source file as an executable, links that file against
# the common test core and creates a test from it. The first parameter is the
# source file, the second is the name of the executable and the test and the
# last parameter is the template suffix of the test. The template should follow
# the following naming policy: `maxscale.cnf.template.<template name>` and the
# file should be located in the /cnf/ directory.
#
# Example: to add simple_test.cpp with maxscale.cnf.template.simple_config to the
# test set, the function should be called as follows:
#     add_test_executable(simple_test.cpp simple_test simple_config LABELS some_label)
function(add_test_executable source name template)
  add_template(${name} ${template})
  add_executable(${name} ${source})
  target_link_libraries(${name} testcore)
  add_test(NAME ${name} COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${name} ${name} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

  list(REMOVE_AT ARGV 0 1 2 3)

  foreach (label IN LISTS ARGV)
    get_property(prev_labels TEST ${name} PROPERTY LABELS)
    set_property(TEST ${name} PROPERTY LABELS ${label} ${prev_labels})
  endforeach()
  set_property(TEST ${name} PROPERTY TIMEOUT ${TIMEOUT})
endfunction()

# Same as add_test_executable, but do not add executable into tests list
function(add_test_executable_notest source name template)
  add_template(${name} ${template})
  add_executable(${name} ${source})
  target_link_libraries(${name} testcore)
endfunction()

# Add a test which uses another test as the executable
function(add_test_derived name executable template)
  add_template(${name} ${template})
  add_test(NAME ${name} COMMAND ${CMAKE_BINARY_DIR}/${executable} ${name} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
  set_property(TEST ${name} PROPERTY TIMEOUT ${TIMEOUT})

  list(REMOVE_AT ARGV 0 1 2)

  foreach (label IN LISTS ARGV)
    get_property(prev_labels TEST ${name} PROPERTY LABELS)
    set_property(TEST ${name} PROPERTY LABELS ${label} ${prev_labels})
  endforeach()
endfunction()

# This function adds a script as a test with the specified name and template.
# The naming of the templates follow the same principles as add_test_executable.
# also suitable for symlinks
function(add_test_script name script template labels)
  add_template(${name} ${template})
  add_test(NAME ${name} COMMAND ${CMAKE_SOURCE_DIR}/${script} ${name} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

  list(REMOVE_AT ARGV 0 1 2)

  foreach (label IN LISTS ARGV)
    get_property(prev_labels TEST ${name} PROPERTY LABELS)
    set_property(TEST ${name} PROPERTY LABELS ${label} ${prev_labels})
  endforeach()
  set_property(TEST ${name} PROPERTY TIMEOUT ${TIMEOUT})
endfunction()

# Label a list of tests as heavy, long running tests
macro(heavy_weight_tests)
  foreach(name IN ITEMS ${ARGN})
    set_property(TEST ${name} PROPERTY LABELS "HEAVY")
  endforeach()
endmacro()

# Label tests as medium weight tests. These tests take more than 180 seconds to complete.
macro(medium_weight_tests)
  foreach(name IN ITEMS ${ARGN})
    set_property(TEST ${name} PROPERTY LABELS "MEDIUM")
  endforeach()
endmacro()

# Label tests as light weight tests. These tests take less than 90 seconds to complete.
macro(light_weight_tests)
  foreach(name IN ITEMS ${ARGN})
    set_property(TEST ${name} PROPERTY LABELS "LIGHT")
  endforeach()
endmacro()

# Unstable tests. Ideally, there should be no tests with this label.
macro(unstable_tests)
  foreach(name IN ITEMS ${ARGN})
    set_property(TEST ${name} PROPERTY LABELS "UNSTABLE")
    set_property(TEST ${name} PROPERTY LABELS "HEAVY")
  endforeach()
endmacro()

# Test utilities
add_test_executable_notest(t.cpp t replication)
add_test_executable_notest(non_native_setup.cpp non_native_setup replication)
add_test_executable_notest(binlog_big_transaction.cpp binlog_big_transaction setup_binlog2)
add_test_executable_notest(avro_long.cpp avro_long avro)
add_test_executable_notest(sysbench_example.cpp sysbench_example replication)

#
# Check that all required components are present. To build even without them,
# add e.g. -DHAVE_PHP=Y to the CMake invocation
#

find_program(HAVE_MYSQLTEST mysqltest)
if (NOT HAVE_MYSQLTEST)
  message(FATAL_ERROR "Could not find mysqltest.")
endif()

find_program(HAVE_PHP php)
if (NOT HAVE_PHP)
  message(FATAL_ERROR "Could not find php.")
endif()
