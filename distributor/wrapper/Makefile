#
# Makefile - Makefile for building distclient wrapper
#

WINDRES = windres
STRIP = strip
CFLAGS = -mwindows -Wall

all: distclient.exe

distclient.exe: LIBS += -lws2_32
distclient.exe: wrapper.c distclient.res
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)
	$(STRIP) $@

%.res: %.rc
	$(WINDRES) $< -O coff -o $@

