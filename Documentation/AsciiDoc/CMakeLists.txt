# add a target to generate documentation from AsciiDoc sources

find_program(ASCIIDOC_EXECUTABLE      asciidoc
  DOC "asciidoc executable")

find_program(A2X_EXECUTABLE           a2x
  DOC "a2x 'asciidoc to anything' executable")

find_program(EBOOK_CONVERT_EXECUTABLE ebook-convert
  DOC "ebook-convert binary, needet to generate .mobi Ebooks for kindle")

if(A2X_EXECUTABLE)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/a2x.conf.in ${CMAKE_CURRENT_BINARY_DIR}/a2x.conf @ONLY)

SET(A2X_OPTS --keep-artifacts --conf-file=a2x.conf --doctype=book)

FILE(GLOB MAXSCALE_DOCS "${CMAKE_CURRENT_SOURCE_DIR}/*.doc")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/maxscale.doc.in ${CMAKE_CURRENT_BINARY_DIR}/maxscale.doc @ONLY)


add_custom_command(
  OUTPUT          maxscale.html
  COMMAND         ${A2X_EXECUTABLE} ${A2X_OPTS} --format=xhtml maxscale.doc
  MAIN_DEPENDENCY maxscale.doc
  DEPENDS         asciidoc.conf ${MAXSCALE_DOCS}
)

add_custom_command(
  OUTPUT          maxscale.chunked
  COMMAND         ${A2X_EXECUTABLE} ${A2X_OPTS} --format=chunked maxscale.doc
  MAIN_DEPENDENCY maxscale.doc
  DEPENDS         asciidoc.conf ${MAXSCALE_DOCS}
)

add_custom_command(
  OUTPUT          maxscale.pdf
  COMMAND         ${A2X_EXECUTABLE} ${A2X_OPTS} --format=pdf maxscale.doc
  MAIN_DEPENDENCY maxscale.doc
  DEPENDS         asciidoc.conf ${MAXSCALE_DOCS}
)

add_custom_command(
  OUTPUT          maxscale.epub
  COMMAND         ${A2X_EXECUTABLE} ${A2X_OPTS} --format=epub maxscale.doc
  MAIN_DEPENDENCY maxscale.doc
  DEPENDS         asciidoc.conf ${MAXSCALE_DOCS}
)

if (EBOOK_CONVERT_EXECUTABLE)
  add_custom_command(
    OUTPUT          maxscale.mobi
    COMMAND         ${EBOOK_CONVERT_EXECUTABLE} maxscale.epub maxscale.mobi >/dev/null
    DEPENDS         ${CMAKE_CURRENT_BINARY_DIR}/maxscale.epub
  )

  SET(MAXSCALE_MOBI maxscale.mobi)
else(EBOOK_CONVERT_EXECUTABLE)
  message(STATUS "no MOBI")
  SET(MAXSCALE_MOBI "")
endif(EBOOK_CONVERT_EXECUTABLE)




configure_file(${CMAKE_CURRENT_SOURCE_DIR}/maxscale.1.doc.in ${CMAKE_CURRENT_BINARY_DIR}/maxscale.1.doc @ONLY)

add_custom_command(
  OUTPUT            maxscale.1
  COMMAND           ${A2X_EXECUTABLE} --doctype=manpage --format=manpage maxscale.1.doc
  MAIN_DEPENDENCY   maxscale.1.doc
  DEPENDS           maxscale.1.doc
)

add_custom_command(
  OUTPUT            maxscale.1.html
  COMMAND           ${A2X_EXECUTABLE} --doctype=manpage --format=xhtml   maxscale.1.doc
  MAIN_DEPENDENCY   maxscale.1.doc
  DEPENDS           maxscale.1.doc
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/maxadmin.1.doc.in ${CMAKE_CURRENT_BINARY_DIR}/maxadmin.1.doc @ONLY)

add_custom_command(
  OUTPUT            maxadmin.1
  COMMAND           ${A2X_EXECUTABLE} --doctype=manpage --format=manpage maxadmin.1.doc
  MAIN_DEPENDENCY   maxadmin.1.doc
  DEPENDS           maxadmin.1.doc
)

add_custom_command(
  OUTPUT            maxadmin.1.html
  COMMAND           ${A2X_EXECUTABLE} --doctype=manpage --format=xhtml   maxadmin.1.doc
  MAIN_DEPENDENCY   maxadmin.1.doc
  DEPENDS           maxadmin.1.doc
)



ADD_CUSTOM_TARGET(asciidoc DEPENDS maxscale.html 
                                   maxscale.chunked 
				   maxscale.pdf 
				   maxscale.epub
				   ${MAXSCALE_MOBI}
				   maxscale.1
				   maxscale.1.html
				   maxadmin.1
				   maxadmin.1.html
                 )

endif(A2X_EXECUTABLE)
