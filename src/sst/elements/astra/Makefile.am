# -*- Makefile -*-
#
#

AM_CPPFLAGS += \
	$(MPI_CPPFLAGS)\
	-I$(top_srcdir)/src

compdir = $(pkglibdir)
comp_LTLIBRARIES = libastra.la

libastra_la_SOURCES = \
	astra.cc \
	astra.h

libastra_la_LDFLAGS = -module -avoid-version # -lAstraSim

EXTRA_DIST =

install-exec-hook:
	$(SST_REGISTER_TOOL) SST_ELEMENT_SOURCE     astra=$(abs_srcdir)
#	$(SST_REGISTER_TOOL) SST_ELEMENT_TESTS      astra=$(abs_srcdir)/tests
