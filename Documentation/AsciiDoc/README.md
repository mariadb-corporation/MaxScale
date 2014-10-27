# Requirements

## must have

* *asciidoc*; including *a2x*
* *docbook* / *dblatex* toolchain (on Debian/Ubuntu that gets installed as a dependency of *asciidoc*)

## optional

* *ebook-convert* from the *calibre* packet -> optional, only needed for .mobi output for kindle
* *graphviz* for the block dependency graph picture in "Debug and Diagnostics"
* *PHP* and *PHP gd image extension* (for now) for the syntax graph in the hint filter module section 

# Building

With old build system it is part of the "documentation" target along with DoxyGen:

  make documentation 

With new CMake build system it has a "doc" target of its own for now:

  make doc

Re-enabling *DoxyGen* is in a separate pull request, as soon as that's merged
it will be possible to combine both in a "doc" target again.

# Editing

The AsciiDoc manual sources are split into several files in *Documentation/AsciiDoc*, roughly one per former GDoc/PDF file.

The top level *maxscale.doc.in *file mostly acts as a skeleton and mostly consists of include:: lines only. (Note: having blank lines between include:: macros seems to be important, otherwise strange DocBook backend failures may happen)

Actual content is in the various other .doc files.

There are also several max*.1.doc.in files, these are used to build unix man pages.

## Macros

Beyond the macros (like include:: and http:) already provided by AsciiDoc there are two additional macro in the local asciidoc.conf file so far

### bug:

the bug: macro can be used to auto-generate links to bug reports on bugs.mariadb.com Bugzilla by just writing e.g.:

  bug:123

to link text "Bug #123" to http://bugs.mariadb.com/show_bug.cgi=id=123

### file:

the file: macro is used to mark file names, these are simply rendered as monospaced <tt> in direct HTML output but are converted into <filename> tags in the DocBook toolchain:

  file:MaxScale.cnf

## Filters

### GraphViz

The GraphViz filter (standard asciidoc filter) is used to render the block diagram in the "Debug and Diagnostics" chapter

### EBNF

A custom EBNF -> Syntax Graph filter has been added, right now it is used to visualize the "hint" syntax in the "hint filter" module section


# Output Customization

TODO:

** title page
** page headers / footers

# AsciiDoc resoures

Project home page: http://asciidoc.org/

Documentation: http://asciidoc.org/userguide.html

A presentation: http://mojavelinux.github.io/decks/asciidoc-with-pleasure/

# Results

See http://php-groupies.de/maxscale/

esp. http://php-groupies.de/maxscale/maxscale.chunked/ch27.html#_descriptor_control_blocks for GraphViz output

and http://php-groupies.de/maxscale/maxscale.chunked/ch03.html#_filter_modules#_hint_syntax for EBNF syntax graph filter output