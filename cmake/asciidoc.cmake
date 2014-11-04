# find required binaries

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
  # create "doc" make target if it doesn't exist yet
  if (NOT TARGET "doc")
    add_custom_target("doc")
  endif (NOT TARGET "doc")

  # global asciidoc config file needs to be preprocessed
  # to replace @CMAKE...DIR@ placeholders with actual paths
  CONFIGURE_FILE(
    ${CMAKE_SOURCE_DIR}/asciidoc/asciidoc.conf.in 
    ${CMAKE_BINARY_DIR}/asciidoc/asciidoc.conf 
   @ONLY
  ) 

  # same for the global a2x config file
  CONFIGURE_FILE(
    ${CMAKE_SOURCE_DIR}/asciidoc/a2x.conf.in 
    ${CMAKE_BINARY_DIR}/asciidoc/a2x.conf 
    @ONLY
  )

  # a2x default command line options
  SET(A2X_OPTS --keep-artifacts --conf-file=${CMAKE_BINARY_DIR}/asciidoc/a2x.conf --doctype=book)

  # check for optional epub converter
  find_program(
    EBOOK_CONVERT_EXECUTABLE 
    ebook-convert
    DOC "ebook-convert binary, needet to generate .mobi Ebooks for kindle"
  )

endif(A2X_EXECUTABLE)

# macro for building a document in multiple formats from asciidoc source
# "basename" is the name of the document, all generated document files
# will start with basename. and basename.doc.in is the main asciidoc 
# source file
# additional asciidoc files included by the main source file can be
# added as dependencies after the basename
MACRO(asciidoc_document basename)
 if(A2X_EXECUTABLE)

  # convert @CMAKE...DIR@ references, eg. in include:: calls
  # and copy file over to build dir (needed as asciidoc isn't
  # really good at having source and output separated)
  CONFIGURE_FILE(
    ${CMAKE_CURRENT_SOURCE_DIR}/${basename}.doc.in 
    ${CMAKE_CURRENT_BINARY_DIR}/${basename}.doc 
    @ONLY
  )

  # single HTML file build rule
  ADD_CUSTOM_COMMAND(
    OUTPUT          ${basename}.html
    COMMAND         ${A2X_EXECUTABLE} ${A2X_OPTS} --format=xhtml ${basename}.doc
    MAIN_DEPENDENCY ${basename}.doc
    DEPENDS         ${basename}.doc ${ARGN}
  )

  # "one file per chapter" HTML build rule
  ADD_CUSTOM_COMMAND(
    OUTPUT          ${basename}.chunked
    COMMAND         ${A2X_EXECUTABLE} ${A2X_OPTS} --format=chunked ${basename}.doc
    MAIN_DEPENDENCY ${basename}.doc
    DEPENDS         ${basename}.doc ${ARGN}
  )

  # PDF build rule
  ADD_CUSTOM_COMMAND(
    OUTPUT          ${basename}.pdf
    COMMAND         ${A2X_EXECUTABLE} ${A2X_OPTS} --format=pdf ${basename}.doc
    MAIN_DEPENDENCY ${basename}.doc
    DEPENDS         ${basename}.doc ${ARGN}
  )

  # .epub format EBook build rule
  ADD_CUSTOM_COMMAND(
    OUTPUT          maxscale.epub
    COMMAND         ${A2X_EXECUTABLE} ${A2X_OPTS} --format=epub maxscale.doc
    MAIN_DEPENDENCY ${basename}.doc
    DEPENDS         ${basename}.doc ${ARGN}
  )

  # optional .mobi format EBook build rule (for Kindle)
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

  # create target for all formats combined
  # TODO: does this work out with parallel builds??
  ADD_CUSTOM_TARGET(
    doc-${basename}
    DEPENDS
      ${basename}.html
      ${basename}.chunked
      ${basename}.pdf
      ${basename}.epub
      ${outvar_mobi}
  )

  # register with main "doc" target
  ADD_DEPENDENCIES(
    doc 
    doc-${basename}
  )
 endif(A2X_EXECUTABLE)
ENDMACRO(asciidoc_document)


# macro for building man pages
# input file name is $manpage.$section.doc.in
# output files are $manpage.$section and $manpage.$section.html
# on "make install" $manpage.$section gets installed in 
# $prefix/man/$section
MACRO(asciidoc_manpage manpage section)
 if(A2X_EXECUTABLE)

  # helper variables 
  SET(invar_base  ${manpage}.${section}.doc)
  SET(invar_txt   ${CMAKE_CURRENT_SOURCE_DIR}/${invar_base}.in)
  SET(outvar_txt  ${CMAKE_CURRENT_BINARY_DIR}/${invar_base})

  # copy file over to builddir, replace @...@ variables
  CONFIGURE_FILE(${invar_txt} ${outvar_txt} @ONLY)

  # build manpage troff file
  ADD_CUSTOM_COMMAND(
    OUTPUT            ${manpage}.${section}
    COMMAND           ${A2X_EXECUTABLE} ${A2X_OPTS} --doctype=manpage --format=manpage ${outvar_txt}
    MAIN_DEPENDENCY   ${invar_base}
    DEPENDS           ${invar_base}
  )

  # add custom target for troff manpage file 
  # ALL makes sure it gets built and installed by default
  ADD_CUSTOM_TARGET(
    doc-man-${manpage}-${section}
    ALL
    DEPENDS ${manpage}.${section}
  )

  # build rule for HTML version
  ADD_CUSTOM_COMMAND(
    OUTPUT            ${manpage}.${section}.html
    COMMAND           ${A2X_EXECUTABLE} ${A2X_OPTS} --doctype=manpage --format=xhtml ${outvar_txt}
    MAIN_DEPENDENCY   ${invar_base}
    DEPENDS           ${invar_base}
  )

  # custom target for HTML version
  ADD_CUSTOM_TARGET(
    doc-html-${manpage}-${section}
    DEPENDS ${manpage}.${section}.html
  )

  # both versions are to be built on "make doc"
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

