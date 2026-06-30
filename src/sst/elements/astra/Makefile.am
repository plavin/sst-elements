# -*- Makefile -*-
#
#

AM_CPPFLAGS += \
	$(MPI_CPPFLAGS)\
	-I$(top_srcdir)/src \
	@ASTRASIM_CPPFLAGS@

compdir = $(pkglibdir)
comp_LTLIBRARIES = libastra.la

libastra_la_SOURCES = \
	astraWorkload.cc \
	astraWorkload.h \
	astraNIC.cc \
	astraNIC.h \
	astraNetworkInterface.cc \
	astraNetworkInterface.h \
	astraEvent.h \
	msgKey.h

libastra_la_LDFLAGS = -module -avoid-version @ASTRASIM_LDFLAGS@

libastra_la_LIBADD   = @ASTRASIM_LIBS@

EXTRA_DIST =

install-exec-hook:
	$(SST_REGISTER_TOOL) SST_ELEMENT_SOURCE     astra=$(abs_srcdir)
#	$(SST_REGISTER_TOOL) SST_ELEMENT_TESTS      astra=$(abs_srcdir)/tests
