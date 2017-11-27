include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../test)

add_executable(test_dbfwfilter
  test_dbfwfilter.cc
  tempfile.cc

  ../../test/filtermodule.cc
  ../../test/mock.cc
  ../../test/mock_backend.cc
  ../../test/mock_client.cc
  ../../test/mock_dcb.cc
  ../../test/mock_routersession.cc
  ../../test/mock_session.cc
  ../../test/module.cc
  ../../test/queryclassifiermodule.cc
)
target_link_libraries(test_dbfwfilter maxscale-common)

add_test(test_dbfwfilter test_dbfwfilter)
