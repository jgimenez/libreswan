# Makefile for pluto/userspace crypto libraries
# Copyright (C) 2005 Michael Richardson <mcr@xelerance.com>
# Copyright (C) 2012 Paul Wouters <paul@libreswan.org>
# 
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2 of the License, or (at your
# option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
# 
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.

LIBRESWANSRCDIR?=$(shell cd ../..; pwd)
srcdir?=$(shell pwd)/
include ${LIBRESWANSRCDIR}/Makefile.inc

SUBDIRS=libaes libdes libmd5 libsha1 libsha2 

ifeq ($(USE_EXTRACRYPTO),true)
SUBDIRS+=libtwofish libserpent
endif

SUBDIRS+=liblswcrypto

def:
	@echo "Please read doc/intro.html or INSTALL before running make"
	@false

# programs

cleanall distclean mostlyclean realclean install programs checkprograms check clean spotless install_file_list:
	@for d in $(SUBDIRS) ; \
	do \
		(cd $$d && $(MAKE) srcdir=${srcdir}$$d/ LIBRESWANSRCDIR=$(LIBRESWANSRCDIR) $@ ) || exit 1; \
	done; \

