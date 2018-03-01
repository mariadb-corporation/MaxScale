# Helper functions
function(debugmsg MSG)
  if(DEBUG_OUTPUT)
	message(STATUS "DEBUG: ${MSG}")
  endif()
endfunction()

macro(check_dirs)

endmacro()

function(subdirs VAR DIRPATH)

  if(${CMAKE_VERSION} VERSION_LESS 2.8.12 )
    set(COMP_VAR PATH)
  else()
    set(COMP_VAR DIRECTORY)
  endif()
  file(GLOB_RECURSE SDIR ${DIRPATH}/*)
  foreach(LOOP ${SDIR})
	get_filename_component(LOOP ${LOOP} ${COMP_VAR})
	list(APPEND ALLDIRS ${LOOP})
  endforeach()
  list(REMOVE_DUPLICATES ALLDIRS)
  set(${VAR} "${ALLDIRS}" CACHE PATH " " FORCE)

endfunction()
