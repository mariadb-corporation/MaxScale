DOC_SRC := Makefile \
           maxscale.doc \
           maxadmin.doc \
           configuration-usage.doc \
           debug.doc \
           writing_modules.doc \
           filter-qla.doc \
           filter-regex.doc \
           filter-tee.doc \
           filter-top.doc \
           mysql-cluster.doc \
	   rabbitmq.doc \
           changelog-0.5.0.doc \
           changelog-0.6.0.doc \
           changelog-0.7.0.doc \
           changelog-1.0.0.doc \
           changelog-1.0.1beta.doc

A2X           := a2x
XSLTPROC_OPTS := --xsltproc-opts "--stringparam toc.section.depth 1 --stringparam generate.section.toc.level 1"
DBLATEX_OPTS  := --dblatex-opts "-P doc.publisher.show=0"
A2X_FLAGS     := $(EXTRA_OPTS) -d book -k $(XSLTPROC_OPTS) $(DBLATEX_OPTS)

documentation: maxscale.html maxscale.pdf maxscale.epub.d

maxscale.html: $(DOC_SRC)
	$(A2X) $(A2X_FLAGS) -f xhtml   maxscale.doc
	$(A2X) $(A2X_FLAGS) -f chunked maxscale.doc

maxscale.pdf: $(DOC_SRC)
	$(A2X) $(A2X_FLAGS) -f pdf maxscale.doc

maxscale.epub.d: $(DOC_SRC)
	$(A2X) $(A2X_FLAGS) -f epub maxscale.doc && touch $@ 

clean:
	@rm -rf *.pdf *.html *.css *.chunked *.epub *.epub.d *.xml
