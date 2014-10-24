BASE_FILES := Makefile asciidoc.conf

FULL_SRC := $(BASE_FILES) \
            asciidoc.conf \
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
ASCIIDOC_OPTS := --asciidoc-opts "-f asciidoc.conf"
A2X_FLAGS     := $(EXTRA_OPTS) -d book -k $(ASCIIDOC_OPTS) $(XSLTPROC_OPTS) $(DBLATEX_OPTS)

EBOOK_CONVERT := ebook-convert 

documentation: maxscale.html maxscale.chunked maxscale.pdf maxscale.epub maxscale.mobi

maxscale.html: $(FULL_SRC)
	$(A2X) $(A2X_FLAGS) -f xhtml   maxscale.doc

maxscale.chunked: $(FULL_SRC)
	$(A2X) $(A2X_FLAGS) -f chunked maxscale.doc && touch $@

maxscale.pdf: $(FULL_SRC)
	$(A2X) $(A2X_FLAGS) -f pdf maxscale.doc

maxscale.fop.pdf: $(FULL_SRC)
	$(A2X) $(A2X_FLAGS) -f pdf --fop maxscale.doc

maxscale.epub: $(FULL_SRC)
	$(A2X) $(A2X_FLAGS) -f epub maxscale.doc

maxscale.mobi: maxscale.epub
	$(EBOOK_CONVERT) $< $@

clean:
	@rm -rf *.pdf *.html *.css *.chunked *.epub *.epub.d *.xml
