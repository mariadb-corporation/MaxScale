= Requirements =

* asciidoc; including a2x
* docbook / dblatex toolchain (on debian/ubuntu that gets installed as a dependency of asciidoc)
* graphviz for the block dependency graph picture in "Debug and Diagnostics"
* ebook-convert from the "calibre" packe -> optional, only needed for .mobi output for kindle

= Building =

With old build system it is part of the "documentation" target along with DoxyGen:

  make documentation 

With new CMake build system it has an "asciidoc" target of its own for now:

  make asciidoc

Re-enabling DoxyGen is in a separate pull request, as soon as that's merged
it will be possible to combine both in a "documentation" target again.

= Editing =

The AsciiDoc manual sources are split into several files in Documentation/AsciiDoc, roughly one per former GDoc/PDF file.

The top level maxscale.doc.in file mostly acts as a skeleton and mostly consists of include:: lines only. (Note: having blank lines between include:: macros seems to be important, otherwise strange DocBook backend failures may happen)

Actual content is in the various other .doc files.

== Macros ==

Beyond the macros (like include:: and http:) already provided by AsciiDoc there is one additional macro in the local asciidoc.conf file so far that can be used to auto-generate links to bug reports on bugs.mariadb.com by just writing e.g.:

  bug:123

to link text "Bug #123" to http://bugs.mariadb.com/show_bug.cgi=id=123

= Output Customization =

TODO

= AsciiDoc resoures =

Project home page: http://asciidoc.org/

Documentation: http://asciidoc.org/userguide.html

