# Function for declaring java tests
#
# name        Name of the test
# src         Test source files
# entry_point The entry point of the JAR file
# template    The configuration template for this test
#
function(add_java_test name src entry_point template)
  add_jar(${name} SOURCES ${src} ${MXS_JAR}
    ENTRY_POINT ${entry_point} INCLUDE_JARS ${MXS_JAR} ${JDBC_JAR})
  add_test(NAME ${name} COMMAND java
    -cp ${TEST_JARPATH}:${CMAKE_CURRENT_BINARY_DIR}/${name}.jar
    ${entry_point}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
  add_template(${name} ${template})
  add_dependencies(${name} maxscale_java)
  set_property(TEST ${name} PROPERTY LABELS java)
endfunction()

# Some constants that make changing the connector easier
set(JDBC_JAR ${CMAKE_CURRENT_SOURCE_DIR}/mariadb-java-client-1.5.4.jar CACHE INTERNAL "")
set(MXS_JAR ${CMAKE_CURRENT_BINARY_DIR}/maxscale_java.jar CACHE INTERNAL "")
set(TEST_JARPATH "${MXS_JAR}:${JDBC_JAR}" CACHE INTERNAL "")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/MaxScaleConfiguration.java.in ${CMAKE_CURRENT_BINARY_DIR}/MaxScaleConfiguration.java @ONLY)
add_jar(maxscale_java SOURCES MaxScaleConnection.java MaxScaleConfiguration.java
  INCLUDE_JARS mariadb-java-client-1.5.4.jar)
add_subdirectory(test1)
add_subdirectory(prep_stmt)
add_subdirectory(batch)
