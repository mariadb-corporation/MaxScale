DOC_SRC := maxscale.doc maxadmin.doc configuration-usage.doc changelog-0.5.0.doc changelog-0.6.0.doc changelog-0.7.0.doc changelog-1.0.0.doc


A2X       := a2x
A2X_FLAGS := -d book -k --xsltproc-opts "--stringparam toc.section.depth 1 --stringparam generate.section.toc.level 1"

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
