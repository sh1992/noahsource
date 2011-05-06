#!/usr/bin/make -f
#
# Makefile for project
#

override CFLAGS += -Wall -DDEBUG -lm -lpthread -g -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
all: ga-numbers ga-spectroscopy ga-spectroscopy-client

# GA test program
ga-numbers: CFLAGS += -DGA_segment=uint32_t -DGA_segment_size=32
ga-numbers: DEPS = ga.c
ga-numbers: $(DEPS) ga.usage.h ga.h

# ga-spectroscopy
SPECFLAGS = -DGA_segment=uint32_t -DGA_segment_size=32 -DTHREADS
ga-spectroscopy: CFLAGS += $(SPECFLAGS)
ga-spectroscopy: DEPS = ga.c
ga-spectroscopy: $(DEPS) ga-spectroscopy.checksum.h ga.usage.h ga.h
# spcat.a

# ga-spectroscopy-client (client-only binary)
ga-spectroscopy-client: CFLAGS += $(SPECFLAGS) -DCLIENT_ONLY
ga-spectroscopy-client: DEPS = 
.INTERMEDIATE: ga-spectroscopy-client.c
ga-spectroscopy-client.c: ga-spectroscopy.c
	$(SHELL) -c '(echo "#line 1" "\"$<\"";cat $<) > $@'
ga-spectroscopy-client: $(DEPS) ga-spectroscopy.checksum.h ga-clientonly.h

# Checksum file
%.checksum.h: %.c ga.c
	$(SHELL) -c '(echo "char *CHECKSUM = \""`cat $^ | md5sum | cut -d" " -f1`"\";") > $@'
%.usage.h: %.c
	perl generate-usage.pl $^

# Implicit rule for executables
%: %.c
	$(CC) $(CFLAGS) $< $(DEPS) -o $@

# spcat.a library (Integrated SPCAT)
spcat.a:
	$(MAKE) -C spcat-obj spcat.a
	cp -p spcat-obj/spcat.a .

clean:
	-$(MAKE) -C spcat-obj clean
	-rm -f *.o *.a *.checksum.h
	-rm -f ga-numbers ga-numbers.exe
	-rm -f ga-spectroscopy ga-spectroscopy.exe
	-rm -f ga-spectroscopy-client ga-spectroscopy-client.exe
	-rm -f ga-spectroscopy-client.c
	-rm -rf doc/{html,latex}

doc: Doxyfile *.c *.h .PHONY
	PATH=. ga-spectroscopy --help > doc/ga-spectroscopy.txt
	doxygen Doxyfile
	cp -p doc/mydoxygen.sty doc/latex
	$(MAKE) -C doc/latex pdf

.PHONY:
