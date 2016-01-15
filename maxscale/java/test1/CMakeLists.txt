add_jar(simplejavatest SOURCES SimpleConnectorJTest.java MaxScaleConnection.java
  ENTRY_POINT maxscale.java.test1.SimpleConnectorJTest INCLUDE_JARS mariadb-java-client-1.3.3.jar)
add_test(NAME simplejavatest COMMAND java
  -cp ${CMAKE_CURRENT_BINARY_DIR}/simplejavatest.jar:${CMAKE_CURRENT_SOURCE_DIR}/mariadb-java-client-1.3.3.jar 
  maxscale.java.test1.SimpleConnectorJTest)
