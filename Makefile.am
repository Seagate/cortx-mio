# Copyright: (c) 2020 - 2021 Seagate Technology LLC and/or its its Affiliates,
# All Rights Reserved
#
# This software is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

#-------------------------------------------------------------------------------
#                             Quick Start                                      #
#-------------------------------------------------------------------------------

#-------------------------------------------------------------------------------
#                            Automake configuration                            #
#-------------------------------------------------------------------------------

# control verbosity level of make depending on 'V' command-line argument and
# --enable-silent-rules option of configure script
make_verbose   = $(make_verbose_$(V))
make_verbose_  = $(make_verbose_$(AM_DEFAULT_VERBOSITY))
make_verbose_0 = --no-print-directory

MAKEFLAGS = $(make_verbose)

# in order to suppress annoying automake warning "CFLAGS: non-POSIX variable name"
# -Wno-portability automake option should be set for this makefile, this is
# required for xcode generation
AUTOMAKE_OPTIONS = -Wno-portability

# required to properly rebuild aclocal.m4 macros on configure.ac or Makefile.am
# updates
ACLOCAL_AMFLAGS = -I m4

# default XXXFLAGS, used for preprocessing, compiling and linking for all
# user-space targets

AM_CPPFLAGS  = @MIO_CPPFLAGS@
AM_CFLAGS    = @MIO_CFLAGS@
AM_LDFLAGS   = @MIO_LDFLAGS@

AM_DISTCHECK_CONFIGURE_FLAGS = --with-libmero=@LIBMERO@ --with-mero-headers=@MERO_HEADERS@

#-------------------------------------------------------------------------------
#                               Global variables                               #
#-------------------------------------------------------------------------------

# initialize variables, so values can be appended later with += if needed

# everything that needs to be installed/packaged goes here
bin_PROGRAMS       =
lib_LTLIBRARIES    =

# non-installable/packageable stuff: build helpers, local tests, etc.
noinst_PROGRAMS    =
noinst_LTLIBRARIES =

# documentation
man_MANS   =

# Extra files with distribution
EXTRA_DIST = autogen.sh

# Files to clean
CLEANFILES =

#-------------------------------------------------------------------------------
#                                  MIO Libraries                               #
#-------------------------------------------------------------------------------

lib_LTLIBRARIES          += lib/libmio.la
lib_libmio_la_CPPFLAGS  = -DM0_TARGET='libmero' $(AM_CPPFLAGS)
lib_libmio_la_LDFLAGS   = -version-info @LT_VERSION@ $(AM_LDFLAGS)
#lib_libmio_la_LIBADD    = @MERO_LIBS@ @YAML_LIBS@ 
lib_libmio_la_LIBADD    = -lmero -lyaml 

lib_libmio_la_SOURCES =

# install directory for public libmio headers
mio_includedir             = $(includedir)/mio

nobase_mio_include_HEADERS = config.h

include $(top_srcdir)/src/Makefile.sub


#-------------------------------------------------------------------------------
#                                 Tests                                        #
#-------------------------------------------------------------------------------
include $(top_srcdir)/tests/Makefile.sub


#-------------------------------------------------------------------------------
#                                 MIO Examples                                 #
#-------------------------------------------------------------------------------

noinst_PROGRAMS                   += examples/mio_cat 
noinst_PROGRAMS                   += examples/mio_copy 
noinst_PROGRAMS                   += examples/mio_touch 
noinst_PROGRAMS                   += examples/mio_unlink 
noinst_PROGRAMS                   += examples/mio_hint_set
noinst_PROGRAMS                   += examples/mio_hint_stat
noinst_PROGRAMS                   += examples/mio_kvs_create_set 
noinst_PROGRAMS                   += examples/mio_kvs_del_set 
noinst_PROGRAMS                   += examples/mio_kvs_insert 
noinst_PROGRAMS                   += examples/mio_kvs_retrieve 
noinst_PROGRAMS                   += examples/mio_kvs_del_pairs 
noinst_PROGRAMS                   += examples/mio_comp_obj_example 
noinst_PROGRAMS                   += examples/mio_rw_threads 

examples_mio_cat_CPPFLAGS = -DMIO_TARGET='mio_cat' $(AM_CPPFLAGS)
examples_mio_cat_LDADD    = $(top_builddir)/lib/libmio.la

examples_mio_copy_CPPFLAGS = -DMIO_TARGET='mio_copy' $(AM_CPPFLAGS)
examples_mio_copy_LDADD    = $(top_builddir)/lib/libmio.la

examples_mio_touch_CPPFLAGS = -DMIO_TARGET='mio_touch' $(AM_CPPFLAGS)
examples_mio_touch_LDADD    = $(top_builddir)/lib/libmio.la

examples_mio_unlink_CPPFLAGS = -DMIO_TARGET='mio_unlink' $(AM_CPPFLAGS)
examples_mio_unlink_LDADD    = $(top_builddir)/lib/libmio.la

examples_mio_hint_set_CPPFLAGS = -DMIO_TARGET='mio_hint_set' $(AM_CPPFLAGS)
examples_mio_hint_set_LDADD    = $(top_builddir)/lib/libmio.la

examples_mio_hint_stat_CPPFLAGS = -DMIO_TARGET='mio_hint_stat' $(AM_CPPFLAGS)
examples_mio_hint_stat_LDADD    = $(top_builddir)/lib/libmio.la

examples_mio_kvs_create_set_CPPFLAGS = -DMIO_TARGET='mio_kvs_create_set' $(AM_CPPFLAGS)
examples_mio_kvs_create_set_LDADD    = $(top_builddir)/lib/libmio.la

examples_mio_kvs_del_set_CPPFLAGS = -DMIO_TARGET='mio_kvs_del_set' $(AM_CPPFLAGS)
examples_mio_kvs_del_set_LDADD    = $(top_builddir)/lib/libmio.la

examples_mio_kvs_insert_CPPFLAGS = -DMIO_TARGET='mio_kvs_insert' $(AM_CPPFLAGS)
examples_mio_kvs_insert_LDADD    = $(top_builddir)/lib/libmio.la

examples_mio_kvs_retrieve_CPPFLAGS = -DMIO_TARGET='mio_kvs_retrieve' $(AM_CPPFLAGS)
examples_mio_kvs_retrieve_LDADD    = $(top_builddir)/lib/libmio.la

examples_mio_kvs_del_pairs_CPPFLAGS = -DMIO_TARGET='mio_kvs_del_pairs' $(AM_CPPFLAGS)
examples_mio_kvs_del_pairs_LDADD    = $(top_builddir)/lib/libmio.la

examples_mio_comp_obj_example_CPPFLAGS = -DMIO_TARGET='mio_comp_obj_example' $(AM_CPPFLAGS)
examples_mio_comp_obj_example_LDADD    = $(top_builddir)/lib/libmio.la

examples_mio_rw_threads_CPPFLAGS = -DMIO_TARGET='mio_comp_obj_example' $(AM_CPPFLAGS)
examples_mio_rw_threads_LDADD    = $(top_builddir)/lib/libmio.la -lpthread -lcrypto -lssl

include $(top_srcdir)/examples/Makefile.sub

#-------------------------------------------------------------------------------
#                                 Build rules                                  # 
#-------------------------------------------------------------------------------

# vim: textwidth=80 nowrap foldmethod=marker
