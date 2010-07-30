#!/usr/bin/make -f
#
# Makefile for spcat-obj. Mostly the same as Makefile.orig
#

#next line for atlas blas
#BLASLIB=-lcblas -latlas
#next line for fortran blas and cblas wrappers
#BLASLIB=-lcblas -lblas
#next line for supplied routines
BLASLIB=
ifndef ($(BLASLIB))
    LBLAS=dblas.o
endif

override CFLAGS += -Wall -g
%.o: CFLAGS += -Dtmpfile=TMPFILE_DISUSED -Dfopen=FOPEN_DISUSED
spcat: CFLAGS += -lm

all: spcat.a spcat
default: all
clean:
	rm -f *.o *.a spcat
spcat: spcat.c spcat.a spcat-obj.h calpgm.h spinit.h
tmpfile-fopencookie.o: tmpfile-fopencookie.c
# Similar as spcat rule in Makefile.orig
spcat.a: spcat-obj.o tmpfile-fopencookie.o spinv.o spinit.o catutil.o ulib.o slibgcc.o cnjj.o $(LBLAS)
	ar rs $@ $^
# From Makefile.orig
cnjj.o:cnjj.c cnjj.h
dblas.o: dblas.c cblas.h
#lsqfit.o:lsqfit.c lsqfit.h
#memstream-sortsub.o: memstream-sortsub.c calpgm.h
slibgcc.o:slibgcc.c slib.h
spcat-obj.o:spcat-obj.c calpgm.h spcat-obj.h tmpfile-fopencookie.h spinit.h
spinit.o:spinit.c calpgm.h spinit.h spcat-obj.h calpgm.h
spinv.o:spinv.c calpgm.h spinit.h spcat-obj.h calpgm.h
ulib.o:ulib.c calpgm.h
catutil.o:catutil.c catutil.h
