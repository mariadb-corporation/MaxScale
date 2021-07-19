# Default test timeout
set(TEST_TIMEOUT 1200)
# Return code for skipped tests
set(TEST_SKIP_RC 202)

# Adds linebreaks to curly brackets in a variable.
function(add_linebreaks source_var dest_var)
  string(REPLACE }, },\n splitted "${${source_var}}")
  set(${dest_var} ${splitted} PARENT_SCOPE)
endfunction()

# Helper function to add a configuration template to the global test definitions list.
# Parameters are as in add_test_executable().
function(add_template name template labels)
  set(config_template_path "${CMAKE_SOURCE_DIR}/system-test/cnf/maxscale.cnf.template.${template}")
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
  set_property(TEST ${name} PROPERTY SKIP_RETURN_CODE ${TEST_SKIP_RC})
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

# Same as "add_test_executable" but with a local config template file. Called using named arguments as in
# add_test_executable_ex(NAME <testname> SOURCE <source.cc> CONFIG <configfile.cnf> VMS <backends setup>
# LABELS <label list>).
#
# If creating a derived test, denote the original test with ORIG_NAME.
function(add_test_executable_ex)
  set(arg_names NAME SOURCE CONFIG LIBS VMS LABELS ORIG_NAME)
  set(now_parsing "")
  foreach(elem ${ARGN})
    list(FIND arg_names ${elem} arg_names_ind)
    if (arg_names_ind GREATER -1)
      set(now_parsing ${elem})
    else()
      # Add element to target list depending on last argument name.
      if (NOT now_parsing)
        message(FATAL_ERROR "${elem} is not preceded by a valid argument name. Argument names are "
                "${arg_names}")
      elseif("${now_parsing}" STREQUAL "NAME")
        list(APPEND name ${elem})
      elseif("${now_parsing}" STREQUAL "SOURCE")
        list(APPEND source_file ${elem})
      elseif("${now_parsing}" STREQUAL "CONFIG")
        list(APPEND config_files ${elem})
      elseif("${now_parsing}" STREQUAL "LIBS")
        list(APPEND link_libraries ${elem})
      elseif("${now_parsing}" STREQUAL "VMS")
        list(APPEND vms_setup ${elem})
      elseif("${now_parsing}" STREQUAL "LABELS")
        list(APPEND labels ${elem})
      elseif("${now_parsing}" STREQUAL "ORIG_NAME")
        list(APPEND orig_name ${elem})
      endif()
    endif()
  endforeach()

  # Check that name, source and config are single-valued.
  set(errmsg "is not set or has multiple values.")

  list(LENGTH name list_len)
  if (NOT ${list_len} EQUAL 1)
    message(FATAL_ERROR "NAME ${errmsg}")
  endif()

  # If original name was given, source file should not.
  list(LENGTH orig_name n_orig_name)
  list(LENGTH source_file n_source_file)

  if ("${n_orig_name}" GREATER 1)
    message(FATAL_ERROR "ORIG_NAME has multiple values.")
  elseif("${n_orig_name}" EQUAL 1)

    if ("${n_source_file}" GREATER 0)
      message(FATAL_ERROR "Both ORIG_NAME and SOURCE are defined.")
    endif()

    # Also check link libraries
    list(LENGTH link_libraries n_link_libraries)
    if ("${n_link_libraries}" GREATER 0)
      message(FATAL_ERROR "Both ORIG_NAME and LIBS are defined.")
    endif()

    get_test_property(${orig_name} TIMEOUT to)
    if (NOT to)
      message(FATAL_ERROR "${orig_name} is not an existing test.")
    endif()

  elseif(NOT ${n_source_file} EQUAL 1)
    message(FATAL_ERROR "SOURCE ${errmsg}")
  endif()

  list(LENGTH config_files list_len)
  if (${list_len} LESS 1)
    message(FATAL_ERROR "CONFIG is not set")
  endif()

  # VMS may be multivalued. If not using any backends, the value should be "none".
  list(LENGTH vms_setup list_len)
  if (${list_len} LESS 1)
    message(FATAL_ERROR "VMS is not set.")
  else()
    # Check that the vms setup is recognized.
    set(known_vms_setups none repl_backend galera_backend big_repl_backend columnstore_backend second_maxscale backend_ssl)
    foreach(elem ${vms_setup})
      list(FIND known_vms_setups ${elem} vms_ind)
      if (vms_ind GREATER -1)
        if (NOT ${elem} STREQUAL "none")
          # MDBCI-labels are in caps.
          string(TOUPPER ${elem} elem_upper)
          list(APPEND vms_upper ${elem_upper})
        endif()
      else()
        message(FATAL_ERROR "${elem} is not a valid VM setup. Valid values are " "${known_vms_setups}")
      endif()
    endforeach()
  endif()

  # Config file can be multivalued. Check that the files exist and add to list.
  foreach(elem ${config_files})
    set(cnf_file_path ${CMAKE_CURRENT_SOURCE_DIR}/${elem})
    if (NOT EXISTS ${cnf_file_path})
      message(FATAL_ERROR "Config file ${cnf_file_path} not found.")
    endif()
    list(APPEND cnf_file_path_total ${cnf_file_path})
  endforeach()

  # Add test name, config file(s) and label(s) to the total test definitions variable,
  # which will be written to test_info.cc.
  set(new_def "{\"${name}\", \"${cnf_file_path_total}\", \"${vms_upper}\"}")
  set(TEST_DEFINITIONS "${TEST_DEFINITIONS}${new_def}," CACHE INTERNAL "")

  if ("${n_source_file}" EQUAL 1)
    add_executable(${name} ${source_file})
    list(APPEND link_libraries maxtest)
    target_link_libraries(${name} ${link_libraries})
    add_test(NAME ${name} COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${name} ${name} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  else()
    add_test(NAME ${name} COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${orig_name} ${name} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  endif()

  list(APPEND ctest_labels ${vms_upper} ${labels})
  add_test_properties(${name} ${ctest_labels})
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

find_package(CURL REQUIRED)
