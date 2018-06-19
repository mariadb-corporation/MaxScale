set(TYPES "*.h" "*.hh")
foreach(type ${TYPES})
  file(GLOB HEADERS ${type})
  foreach(var ${HEADERS})
    get_filename_component(header ${var} NAME)
    install(FILES ${header} DESTINATION include/maxbase)
  endforeach()
endforeach()


