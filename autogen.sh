#!/bin/bash -e

# This script does all the magic calls to automake/autoconf and
# friends that are needed to configure a cvs checkout.  As described in
# the file HACKING you need a couple of extra tools to run this script
# successfully.
#
# If you are compiling from a released tarball you don't need these
# tools and you shouldn't use this script.  Just call ./configure
# directly.

test -n "$srcdir" || srcdir=`dirname "$0"`
test -n "$srcdir" || srcdir=.

olddir=`pwd`
cd $srcdir

AUTORECONF=`which autoreconf`
if test -z $AUTORECONF; then
        echo "*** No autoreconf found, please install it ***"
        exit 1
fi

INTLTOOLIZE=`which intltoolize`
if test -z $INTLTOOLIZE; then
        echo "*** No intltoolize found, please install the intltool package ***"
        exit 1
fi

autopoint --force
AUTOPOINT='intltoolize --automake --copy' autoreconf --force --install --verbose

# Patch the generated po/Makefile.in.in file so that locale files are installed
# in the correct location on OS X and Free-BSD systems.  This is a workaround
# for a bug in intltool.  See https://launchpad.net/bugs/398571
#
# TODO: Drop this hack, and bump our intltool version requiement once the issue
#       is fixed in intltool
sed 's/itlocaledir = $(prefix)\/$(DATADIRNAME)\/locale/itlocaledir = $(datarootdir)\/locale/' < po/Makefile.in.in > po/Makefile.in.in.tmp
mv po/Makefile.in.in.tmp po/Makefile.in.in

echo ""
echo "Done!  Please run './configure' now."
