#!/usr/bin/make -f
#
# Makefile for project
#

override CFLAGS += -Wall -DDEBUG -lm -lpthread -g -D_GNU_SOURCE
ga-spectroscopy: CFLAGS += -DGA_segment=uint32_t -DGA_segment_size=32 -DTHREADS

all: ga-numbers ga-spectroscopy

ga-numbers: ga.c

ga-spectroscopy: ga.c spcat.a

spcat.a:
	make -C spcat-obj spcat.a
	cp -p spcat-obj/spcat.a .

clean:
	-make -C spcat-obj clean
	-rm -f *.o *.a ga-numbers ga-spectroscopy
	-rm -rf doc/{html,latex}

doc: Doxyfile *.c *.h .PHONY
	PATH=. ga-spectroscopy --help > doc/ga-spectroscopy.txt
	doxygen Doxyfile
	cp -p doc/mydoxygen.sty doc/latex
	make -C doc/latex pdf

.PHONY:
