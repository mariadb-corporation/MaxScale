# add a target to generate documentation from AsciiDoc sources

find_program(ASCIIDOC_EXECUTABLE      asciidoc
  DOC "asciidoc executable")

find_program(A2X_EXECUTABLE           a2x
  DOC "a2x 'asciidoc to anything' executable")

find_program(EBOOK_CONVERT_EXECUTABLE ebook-convert
  DOC "ebook-convert binary, needet to generate .mobi Ebooks for kindle")

if(A2X_EXECUTABLE)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/maxscale.doc.in ${CMAKE_CURRENT_BINARY_DIR}/maxscale.doc @ONLY)

SET(XSLTPROC_OPTS --xsltproc-opts '--stringparam toc.section.depth 1 --stringparam generate.section.toc.level 1')
SET(DBLATEX_OPTS  --dblatex-opts '-P doc.publisher.show=0')
SET(ASCIIDOC_OPTS --asciidoc-opts '-f ${CMAKE_CURRENT_SOURCE_DIR}/asciidoc.conf')

SET(A2X_OPTS      -d book ${ASCIIDOC_OPTS} ${XSLTPROC_OPTS} ${DBLATEX_OPTS} -f)

FILE(GLOB MAXSCALE_DOCS "${CMAKE_CURRENT_SOURCE_DIR}/*.doc")

add_custom_command(
  OUTPUT          maxscale.html
  COMMAND         ${A2X_EXECUTABLE} ${A2X_OPTS} xhtml maxscale.doc
  MAIN_DEPENDENCY maxscale.doc
  DEPENDS         asciidoc.conf ${MAXSCALE_DOCS}
)

add_custom_command(
  OUTPUT          maxscale.chunked
  COMMAND         ${A2X_EXECUTABLE} ${A2X_OPTS} chunked maxscale.doc
  MAIN_DEPENDENCY maxscale.doc
  DEPENDS         asciidoc.conf ${MAXSCALE_DOCS}
)

add_custom_command(
  OUTPUT          maxscale.pdf
  COMMAND         ${A2X_EXECUTABLE} ${A2X_OPTS} pdf maxscale.doc
  MAIN_DEPENDENCY maxscale.doc
  DEPENDS         asciidoc.conf ${MAXSCALE_DOCS}
)

add_custom_command(
  OUTPUT          maxscale.epub
  COMMAND         ${A2X_EXECUTABLE} ${A2X_OPTS} epub maxscale.doc
  MAIN_DEPENDENCY maxscale.doc
  DEPENDS         asciidoc.conf ${MAXSCALE_DOCS}
)

if (EBOOK_CONVERT_EXECUTABLE)
  message(STATUS "MOBI")
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

ADD_CUSTOM_TARGET(asciidoc DEPENDS maxscale.html 
                                   maxscale.chunked 
				   maxscale.pdf 
				   maxscale.epub
				   ${MAXSCALE_MOBI}
		  COMMENT "build documentation from asciidoc sources"
                 )

endif(A2X_EXECUTABLE)
