configure_file(${CMAKE_CURRENT_SOURCE_DIR}/MaxScaleConfiguration.java.in ${CMAKE_CURRENT_BINARY_DIR}/MaxScaleConfiguration.java @ONLY)
add_jar(maxscale_java SOURCES MaxScaleConnection.java MaxScaleConfiguration.java
  INCLUDE_JARS mariadb-java-client-1.3.3.jar)
add_subdirectory(test1)
