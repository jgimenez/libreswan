# I really am not happy about using symlinks to make this work.
#
# I think that there should be a better way to do this.
# see module26.sh in packaging/makefiles
#

LIBRESWANSRCDIR?=$(shell cd ../../..;pwd)

include ${LIBRESWANSRCDIR}/Makefile.inc

TESTCASE=spi01

include ${srcdir}../Makefile.pfkey

spi01.o: ../../../programs/spi/spi.c
