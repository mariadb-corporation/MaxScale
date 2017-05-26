#!/usr/bin/env bash

# This template is really important. It includes changes to make images a reasonable size,
# gracefully handle Header 4 and Header 5 translation to paragraph and subparagraph,
# stick the MariaDB logo at the top of the document, etc.
template=$PWD/maxscale.latex

# make sure we can find LaTeX. feel free to remove this if you have it somewhere more normal


pwd=$PWD

input=$1
if ! shift; then
    echo "ERROR: must specify input filename" >&2
    exit 1
fi

file=${input##*/}
basename=${file%%.*}
basedir=${input%/*}

# we have to cd to the location of the file so that images with relative paths can be found
if ! cd "$basedir"; then
    echo "ERROR: could not cd to $basedir" >&2
    exit 1
fi

# this filter function can be used for some pipline of miscellaneous stuff you want to do to the input file
# if you want to add more filters, you can just build a normal Unix pipeline.
filter(){
    # this instructs pandoc to build a titleblock
    # the idea is that the first line will be something like "MariaDB MaxScale"
    # and the 2nd line will be something like "Configuration & Usage Scenarios".
    # put a hard linebreak between those so they're both part of the "title".
    # pandoc supports another line that is the author. right now I manually make that blank.
    # and we add the current date to the end of the 2 lines in the titleblock
    #date=$(date +"%B %e, %Y")
    printf -v date "%(%B %e, %Y)T"
    #awk ' /^$/ {p++} p==1{printf "%% %s\n", "'"$date"'";p++} !p{printf "%% "} {print} '
    awk ' NR==1{ printf "%% " } # put % in front of first line
          NR==2{ printf "  " } # put some space in front of 2nd line. pandoc requires this to continue the title
          NR==3{ printf "%% %s", "'"$date"'" } # 3rd line becomes the date.
          {printf "%s", $0} # now print whatever was actually on the line. (but leave off the newline) should have been blank for the 3rd line!
          {printf "%s", "\n"} # newline.
          '
}


pandoc_vars=(
    -V fontsize=12pt
    -V version=1.10
    -V geometry:margin=1in
    --toc
    -t latex
    --latex-engine=xelatex
    --template="$template"
)

pandoc  "${pandoc_vars[@]}"  <"$file" -o "${pwd}/${basename}.pdf"

