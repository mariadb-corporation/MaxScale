add_jar(simplejavatest SOURCES SimpleConnectorJTest.java ../maxscale_java.jar
  ENTRY_POINT maxscale.java.test1.SimpleConnectorJTest INCLUDE_JARS maxscale_java ../mariadb-java-client-1.3.3.jar)
#add_test(NAME simplejavatest COMMAND java
#  -cp ${CMAKE_CURRENT_BINARY_DIR}/../maxscale_java.jar:${CMAKE_CURRENT_BINARY_DIR}/simplejavatest.jar:${CMAKE_CURRENT_SOURCE_DIR}/../mariadb-java-client-1.3.3.jar 
#  maxscale.java.test1.SimpleConnectorJTest
#  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
file(APPEND ${CMAKE_BINARY_DIR}/templates "simplejavatest simplejavatest\n")
