find_program(
  ASCIIDOC_EXECUTABLE      
  asciidoc
  DOC "asciidoc executable"
)

find_program(
  A2X_EXECUTABLE           
  a2x
  DOC "a2x 'asciidoc to anything' executable"
)

if(A2X_EXECUTABLE)
  if (NOT TARGET "doc")
    add_custom_target("doc")
  endif (NOT TARGET "doc")

  find_program(
    EBOOK_CONVERT_EXECUTABLE 
    ebook-convert
    DOC "ebook-convert binary, needet to generate .mobi Ebooks for kindle"
  )
endif(A2X_EXECUTABLE)


MACRO(asciidoc_document basename)
 if(A2X_EXECUTABLE)

  CONFIGURE_FILE(
    ${CMAKE_CURRENT_SOURCE_DIR}/${basename}.doc.in 
    ${CMAKE_CURRENT_BINARY_DIR}/${basename}.doc 
    @ONLY
  )

  ADD_CUSTOM_COMMAND(
    OUTPUT          ${basename}.html
    COMMAND         ${A2X_EXECUTABLE} ${A2X_OPTS} --format=xhtml ${basename}.doc
    MAIN_DEPENDENCY ${basename}.doc
    DEPENDS         ${basename}.doc ${ARGN}
  )

  ADD_CUSTOM_COMMAND(
    OUTPUT          ${basename}.chunked
    COMMAND         ${A2X_EXECUTABLE} ${A2X_OPTS} --format=chunked ${basename}.doc
    MAIN_DEPENDENCY ${basename}.doc
    DEPENDS         ${basename}.doc ${ARGN}
  )

  ADD_CUSTOM_COMMAND(
    OUTPUT          ${basename}.pdf
    COMMAND         ${A2X_EXECUTABLE} ${A2X_OPTS} --format=pdf ${basename}.doc
    MAIN_DEPENDENCY ${basename}.doc
    DEPENDS         ${basename}.doc ${ARGN}
  )

  ADD_CUSTOM_COMMAND(
    OUTPUT          maxscale.epub
    COMMAND         ${A2X_EXECUTABLE} ${A2X_OPTS} --format=epub maxscale.doc
    MAIN_DEPENDENCY ${basename}.doc
    DEPENDS         ${basename}.doc ${ARGN}
  )

  IF(EBOOK_CONVERT_EXECUTABLE)
    ADD_CUSTOM_COMMAND(
      OUTPUT  ${basename}.mobi
      COMMAND ${EBOOK_CONVERT_EXECUTABLE} ${basename}.epub ${basename}.mobi >/dev/null
      DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${basename}.epub
  )

    SET(outvar_mobi ${basename}.mobi)
  ELSE(EBOOK_CONVERT_EXECUTABLE)
    SET(outvar_mobi "")
  ENDIF(EBOOK_CONVERT_EXECUTABLE)

  ADD_CUSTOM_TARGET(
    doc-${basename}
    DEPENDS
      ${basename}.html
      ${basename}.chunked
      ${basename}.pdf
      ${basename}.epub
      ${outvar_mobi}
  )

  ADD_DEPENDENCIES(
    doc 
    doc-${basename}
  )
 endif(A2X_EXECUTABLE)
ENDMACRO(asciidoc_document)

MACRO(asciidoc_manpage manpage section)
 if(A2X_EXECUTABLE)

  SET(invar_base  ${manpage}.${section}.doc)
  SET(invar_txt   ${CMAKE_CURRENT_SOURCE_DIR}/${invar_base}.in)
  SET(outvar_txt  ${CMAKE_CURRENT_BINARY_DIR}/${invar_base})

  CONFIGURE_FILE(${invar_txt} ${outvar_txt} @ONLY)

  ADD_CUSTOM_COMMAND(
    OUTPUT            ${manpage}.${section}
    COMMAND           ${A2X_EXECUTABLE} --doctype=manpage --format=manpage ${outvar_txt}
    MAIN_DEPENDENCY   ${invar_base}
    DEPENDS           ${invar_base}
  )

  ADD_CUSTOM_TARGET(
    doc-man-${manpage}-${section}
    ALL
    DEPENDS ${manpage}.${section}
  )

  ADD_CUSTOM_COMMAND(
    OUTPUT            ${manpage}.${section}.html
    COMMAND           ${A2X_EXECUTABLE} --doctype=manpage --format=xhtml ${outvar_txt}
    MAIN_DEPENDENCY   ${invar_base}
    DEPENDS           ${invar_base}
  )

  ADD_CUSTOM_TARGET(
    doc-html-${manpage}-${section}
    DEPENDS ${manpage}.${section}.html
  )

  ADD_DEPENDENCIES(
    doc 
    doc-man-${manpage}-${section}
    doc-html-${manpage}-${section}
  )

  INSTALL(FILES       ${CMAKE_CURRENT_BINARY_DIR}/${manpage}.${section}      
          DESTINATION ${CMAKE_INSTALL_PREFIX}/man/man${section}
	  COMPONENT   documentation)

 endif(A2X_EXECUTABLE)
ENDMACRO(asciidoc_manpage)

