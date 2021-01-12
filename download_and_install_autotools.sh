#!/usr/bin/env bash
set -euo pipefail

#Install location
PREFIX=$HOME/data/local

# Versions to grab
M4_VERSION=1.4.18
AUTOCONF_VERSION=2.69
AUTOMAKE_VERSION=1.16.2
LIBTOOL_VERSION=2.4.6

#Get most recent versions
wget http://ftp.gnu.org/gnu/m4/m4-${M4_VERSION}.tar.gz
wget http://ftp.gnu.org/gnu/autoconf/autoconf-${AUTOCONF_VERSION}.tar.gz
wget http://ftp.gnu.org/gnu/automake/automake-${AUTOMAKE_VERSION}.tar.gz
wget http://ftp.gnu.org/gnu/libtool/libtool-${LIBTOOL_VERSION}.tar.gz

# Decompress
gzip -dc m4-${M4_VERSION}.tar.gz | tar xvf -
gzip -dc autoconf-${AUTOCONF_VERSION}.tar.gz | tar xvf -
gzip -dc automake-${AUTOMAKE_VERSION}.tar.gz | tar xvf -
gzip -dc libtool-${LIBTOOL_VERSION}.tar.gz | tar xvf -

cd m4-${M4_VERSION}
./configure -C --prefix=$PREFIX && make && make install
cd ../autoconf-${AUTOCONF_VERSION}
./configure -C --prefix=$PREFIX && make && make install
cd ../automake-${AUTOMAKE_VERSION}
./configure -C --prefix=$PREFIX && make && make install
cd ../libtool-${LIBTOOL_VERSION}
./configure -C --prefix=$PREFIX && make && make install
