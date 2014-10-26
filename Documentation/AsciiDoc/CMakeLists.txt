CONFIGURE_FILE(
  ${CMAKE_CURRENT_SOURCE_DIR}/filters/ebnf-filter/ebnf-filter.conf.in
  ${CMAKE_CURRENT_BINARY_DIR}/filters/ebnf-filter/ebnf-filter.conf 
  @ONLY
)



FILE(GLOB MAXSCALE_DOCS "${CMAKE_CURRENT_SOURCE_DIR}/*.doc")

asciidoc_document(maxscale ${MAXSCALE_DOCS})

asciidoc_manpage(maxscale 1)
asciidoc_manpage(maxadmin 1)


