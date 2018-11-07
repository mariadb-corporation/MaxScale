if (JANSSON_FOUND)
  add_library(masking SHARED
    maskingfilter.cc
    maskingfilterconfig.cc
    maskingfiltersession.cc
    maskingrules.cc
    )

  target_link_libraries(masking maxscale-common ${JANSSON_LIBRARIES})
  set_target_properties(masking PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
  install_module(masking core)

  if(BUILD_TESTS)
    add_subdirectory(test)
  endif()
else()
  message(STATUS "No Jansson libraries found, not building cache filter.")
endif()
