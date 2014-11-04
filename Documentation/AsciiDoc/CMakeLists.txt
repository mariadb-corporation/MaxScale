# setup for the local EBNF -> syntax graph filter

CONFIGURE_FILE(
  ${CMAKE_CURRENT_SOURCE_DIR}/filters/ebnf-filter/ebnf-filter.conf.in
  ${CMAKE_CURRENT_BINARY_DIR}/filters/ebnf-filter/ebnf-filter.conf 
  @ONLY
)

# main all-in-one document

asciidoc_document(
  maxscale 

  changelog-0.5.0.doc  
  changelog-0.6.0.doc  
  changelog-0.7.0.doc  
  changelog-1.0.0.doc  
  changelog-1.0.1beta.doc  
  configuration-usage.doc  
  debug.doc       
  filter-qla.doc  
  filter-regex.doc  
  filter-tee.doc    
  filter-top.doc  
  maxadmin.doc    
  mysql-cluster.doc  
  rabbitmq.doc
  writing_modules.doc
)

# man pages

asciidoc_manpage(maxscale  1)
asciidoc_manpage(maxadmin  1)
asciidoc_manpage(maxkeys   1)
asciidoc_manpage(maxpasswd 1)

