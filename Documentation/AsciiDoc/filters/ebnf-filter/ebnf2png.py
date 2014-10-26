#!/usr/bin/env python
'''
NAME
    ebnf2png - Converts textual EBNF notation to syntax graphs

SYNOPSIS
    ebnf2png [options] INFILE

DESCRIPTION
    This filter reads EBNF notation text from the input file INFILE 
    (or stdin if INFILE is -), converts it to a syntax diagram 
    (aka. "Railroad graph") and writes it to a trimmed PNG image file.

    This script is a wrapper for the ebnf.php script from the 
    DokuWiki EBNF plugin <http://karmin.ch/ebnf/index>.

OPTIONS
    -o OUTFILE
        The file name of the output file. If not specified the output file is
        named like INFILE but with a .png file name extension.

    -m
        Skip if the PNG output file is newer that than the INFILE.
        Compares timestamps on INFILE and OUTFILE. If
        INFILE is - (stdin) then compares MD5 checksum stored in file
        named like OUTFILE but with a .md5 file name extension.
        The .md5 file is created if the -m option is used and the
        INFILE is - (stdin).

    -v
        Verbosely print processing information to stderr.

    --help, -h
        Print this documentation.

    --version
        Print program version number.

AUTHOR
    Written by Hartmut Holzgraefe <hartmut@mariadb.com>
    based on music2png.py from the asciidoc distribution

COPYING
    Copyright (C) 2014 Hartmut Holzgraefe. Free use of this software is
    granted under the terms of the GNU General Public License (GPL).
'''

# Suppress warning: "the md5 module is deprecated; use hashlib instead"
import warnings
warnings.simplefilter('ignore',DeprecationWarning)

import os, sys, tempfile, md5

VERSION = '0.1.1'

# Globals.
verbose = False

class EApp(Exception): pass     # Application specific exception.

def print_stderr(line):
    sys.stderr.write(line + os.linesep)

def print_verbose(line):
    if verbose:
        print_stderr(line)

def write_file(filename, data, mode='w'):
    f = open(filename, mode)
    try:
        f.write(data)
    finally:
        f.close()

def read_file(filename, mode='r'):
    f = open(filename, mode)
    try:
        return f.read()
    finally:
        f.close()

def run(cmd):
    global verbose
    if not verbose:
        cmd += ' 2>%s' % os.devnull
    print_verbose('executing: %s' % cmd)
    if os.system(cmd):
        raise EApp, 'failed command: %s' % cmd

def ebnf2png(infile, outfile, modified):
    '''Convert EBNF notation in file infile to cropped PNG file named outfile.'''
    outfile = os.path.abspath(outfile)
    outdir = os.path.dirname(outfile)
    if not os.path.isdir(outdir):
        raise EApp, 'directory does not exist: %s' % outdir
    basefile = tempfile.mktemp(dir=os.path.dirname(outfile))
    temps = [basefile + ext for ext in ('.bnf', '.ebnf')]
    skip = False
    if infile == '-':
        source = sys.stdin.read()
        checksum = md5.new(source).digest()
        filename = os.path.splitext(outfile)[0] + '.md5'
        if modified:
            if os.path.isfile(filename) and os.path.isfile(outfile) and \
                    checksum == read_file(filename,'rb'):
                skip = True
            else:
                write_file(filename, checksum, 'wb')
    else:
        if not os.path.isfile(infile):
            raise EApp, 'input file does not exist: %s' % infile
        if modified and os.path.isfile(outfile) and \
                os.path.getmtime(infile) <= os.path.getmtime(outfile):
            skip = True
        source = read_file(infile)
    if skip:
        print_verbose('skipped: no change: %s' % outfile)
        return
    # Write temporary source file.
    ebnf = basefile + '.ebnf'
    png = basefile + '.png'
    write_file(ebnf, source)
    saved_pwd = os.getcwd()
    os.chdir(outdir)
    try:
        scriptdir = os.path.dirname(sys.argv[0])
        run('php %s/ebnf.php "%s"' % (scriptdir, ebnf))
        os.rename(png, outfile)
    finally:
        os.chdir(saved_pwd)
    # Chop the bottom 75 pixels off to get rid of the page footer then crop the
    # music image. The -strip option necessary because FOP does not like the
    # custom PNG color profile used by Lilypond.
    # run('convert "%s" -strip -gravity South -chop 0x75 -trim "%s"' % (outfile, outfile))
    for f in temps:
        if os.path.isfile(f):
            print_verbose('deleting: %s' % f)
            os.remove(f)

def usage(msg=''):
    if msg:
        print_stderr(msg)
    print_stderr('\n'
                 'usage:\n'
                 '    ebnf2png [options] INFILE\n'
                 '\n'
                 'options:\n'
                 '    -o OUTFILE\n'
                 '    -m\n'
                 '    -v\n'
                 '    --help\n'
                 '    --version')

def main():
    # Process command line options.
    global verbose
    outfile = None
    modified = False
    import getopt
    opts,args = getopt.getopt(sys.argv[1:], 'o:mhv', ['help','version'])
    for o,v in opts:
        if o in ('--help','-h'):
            print __doc__
            sys.exit(0)
        if o =='--version':
            print('ebnf2png version %s' % (VERSION,))
            sys.exit(0)
        if o == '-o': outfile = v
        if o == '-m': modified = True
        if o == '-v': verbose = True
    if len(args) != 1:
        usage()
        sys.exit(1)
    infile = args[0]
    if outfile is None:
        if infile == '-':
            usage('OUTFILE must be specified')
            sys.exit(1)
        outfile = os.path.splitext(infile)[0] + '.png'
    # Do the work.
    ebnf2png(infile, outfile, modified)
    # Print something to suppress asciidoc 'no output from filter' warnings.
    if infile == '-':
        sys.stdout.write(' ')

if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except KeyboardInterrupt:
        sys.exit(1)
    except Exception, e:
        print_stderr("%s: %s" % (os.path.basename(sys.argv[0]), str(e)))
        sys.exit(1)
