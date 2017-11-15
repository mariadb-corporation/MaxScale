add_executable(test_dbfwfilter
  test_dbfwfilter.cc

  filtermodule.cc
  mock.cc
  mock_backend.cc
  mock_dcb.cc
  mock_routersession.cc
  mock_session.cc
  mock_upstream.cc
  module.cc
  tempfile.cc
)
target_link_libraries(test_dbfwfilter maxscale-common)

add_test(test_dbfwfilter test_dbfwfilter)
