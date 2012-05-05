#!/usr/bin/make -f
#
# Makefile for project. Requires GNU make.
#

# We use gsl (GNU Scientific Library) for random numbers.
GSL = `gsl-config --cflags --libs` -DHAVE_GSL

# Some systems require additional CFLAGS
# CFLAGS += -I/usr/local/include -L/usr/local/lib

# Some systems have md5 instead of md5sum
MD5SUM = (md5sum || md5)

GAFLAGS = -lpthread $(GSL)
override CFLAGS += -Wall -DDEBUG -lm -g -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
all: ga-numbers ga-spectroscopy ga-spectroscopy-client

# GA test program
ga-numbers: CFLAGS += $(GAFLAGS) -DGA_segment=uint32_t -DGA_segment_size=32
ga-numbers: DEPS = ga.c
ga-numbers: $(DEPS) ga.usage.h ga.h

# ga-spectroscopy
SPECFLAGS = -DGA_segment=uint32_t -DGA_segment_size=32 -DTHREADS
ga-spectroscopy: CFLAGS += $(GAFLAGS) $(SPECFLAGS)
ga-spectroscopy: DEPS = ga.c
ga-spectroscopy: $(DEPS) ga.h
ga-spectroscopy: ga-spectroscopy.checksum.h ga.usage.h ga-spectroscopy.usage.h
# spcat.a

# ga-spectroscopy-client (client-only binary)
ga-spectroscopy-client: CFLAGS += $(SPECFLAGS) -DCLIENT_ONLY
ga-spectroscopy-client: DEPS =
.INTERMEDIATE: ga-spectroscopy-client.c
ga-spectroscopy-client.c: ga-spectroscopy.c
	(echo "#line 1 \"$<\"";cat $<) > $@
ga-spectroscopy-client: $(DEPS) ga-spectroscopy.checksum.h ga-clientonly.h

# Checksum file
%.checksum.h: %.c ga.c
	(echo 'char *CHECKSUM = "'`cat $^ | $(MD5SUM) | cut -c1-32`'";') > $@
%.usage.h: %.c
	if [ -e generate-usage.pl ]; then perl generate-usage.pl $^; else \
	echo 'char*'$(subst -,,$*)'_usage="(see source)\\n";' > $*.usage.h; fi

# Implicit rule for executables
%: %.c
	$(CC) $(CFLAGS) $< $(DEPS) -o $@

# spcat.a library (Integrated SPCAT)
spcat.a:
	$(MAKE) -C spcat-obj spcat.a
	cp -p spcat-obj/spcat.a .

clean:
	-$(MAKE) -C spcat-obj clean
	-rm -f *.o *.a *.checksum.h *.usage.h
	-rm -f ga-numbers ga-numbers.exe
	-rm -f ga-spectroscopy ga-spectroscopy.exe
	-rm -f ga-spectroscopy-client ga-spectroscopy-client.exe
	-rm -f ga-spectroscopy-client.c
	-rm -rf doc/{html,latex}

doc: Doxyfile *.c *.h
	PATH=. ga-spectroscopy --help > doc/ga-spectroscopy.txt
	doxygen Doxyfile
	cp -p doc/mydoxygen.sty doc/latex
	$(MAKE) -C doc/latex pdf

.PHONY: all clean doc
