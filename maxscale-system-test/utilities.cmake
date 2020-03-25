# Default test timeout
set(TEST_TIMEOUT 3600)

# Adds linebreaks to curly brackets in a variable.
function(add_linebreaks source_var dest_var)
  string(REPLACE }, },\n splitted "${${source_var}}")
  set(${dest_var} ${splitted} PARENT_SCOPE)
endfunction()

# Helper function to add a configuration template to the global test definitions list.
# Parameters are as in add_test_executable().
function(add_template name template labels)
  set(config_template_path "${CMAKE_CURRENT_SOURCE_DIR}/cnf/maxscale.cnf.template.${template}")
  set(new_def "{\"${name}\", \"${config_template_path}\", \"${labels}\"}")
  set(TEST_DEFINITIONS "${TEST_DEFINITIONS}${new_def}," CACHE INTERNAL "")
endfunction()

# Helper function to add a configuration template
function(add_template_manual name template)
  add_template(${name} ${template} "CONFIG")
endfunction()

# Helper function for adding properties to a test. Adds the default timeout and labels.
function(add_test_properties name labels)
  list(APPEND labels ${ARGN})
  # Remove the LABELS-string from the list if it's there.
  list(REMOVE_ITEM labels "LABELS")
  set_property(TEST ${name} PROPERTY TIMEOUT ${TEST_TIMEOUT})
  set_property(TEST ${name} APPEND PROPERTY LABELS ${labels})
endfunction()


# This functions adds a source file as an executable, links that file against
# the common test core and creates a test from it.
# Parameters:
# source Test source code file name
# name Name of the generated test executable and the test itself
# template Configuration file template file name. Should only be the last part of the file name. The file
# should be located in the /cnf/ directory and have prefix "maxscale.cnf.template.".
# labels Test labels. The labels can be given as "Label1;Label2;Label3..." or "Label1 Label2 Label3 ..."
# Example: to add simple_test.cpp with maxscale.cnf.template.simple_config to the
# test set, the function should be called as follows:
#     add_test_executable(simple_test.cpp simple_test simple_config LABELS some_label)
function(add_test_executable source name template labels)
  list(APPEND labels ${ARGN})
  add_template(${name} ${template} "${labels}")
  add_executable(${name} ${source})
  target_link_libraries(${name} maxtest)
  add_test(NAME ${name} COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${name} ${name} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  add_test_properties(${name} ${labels})
endfunction()

# Same as add_test_executable, but do not add executable into tests list
function(add_test_executable_notest source name template)
  add_template(${name} ${template} "${ARGN}")
  add_executable(${name} ${source})
  target_link_libraries(${name} maxtest)
endfunction()

# Add a test which uses another test as the executable
function(add_test_derived name executable template labels)
  list(APPEND labels ${ARGN})
  add_template(${name} ${template} "${labels}")
  add_test(NAME ${name} COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${executable} ${name} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  add_test_properties(${name} ${labels})
endfunction()

# This function adds a script as a test with the specified name and template.
# The naming of the templates follow the same principles as add_test_executable.
# also suitable for symlinks
function(add_test_script name script template labels)
  list(APPEND labels ${ARGN})
  add_template(${name} ${template} "${labels}")
  add_test(NAME ${name} COMMAND non_native_setup ${name} ${script} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  add_test_properties(${name} ${labels})
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
add_test_executable_notest(non_native_setup.cpp non_native_setup replication)
add_test_executable_notest(sysbench_example.cpp sysbench_example replication)

#
# Check that all required components are present. To build even without them,
# add e.g. -DHAVE_PHP=Y to the CMake invocation
#

find_program(HAVE_MYSQLTEST mysqltest)
if (NOT HAVE_MYSQLTEST)
  message(FATAL_ERROR "Could not find mysqltest. Add -DHAVE_MYSQLTEST=Y to CMake invocation ignore this.")
endif()

find_program(HAVE_PHP php)
if (NOT HAVE_PHP)
  message(FATAL_ERROR "Could not find php. Add -DHAVE_PHP=Y to CMake invocation ignore this.")
endif()
