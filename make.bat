@echo off
rem
rem make.bat - Compile ga-spectroscopy-client using MinGW
rem

path c:\mingw\bin;c:\mingw\msys\1.0\bin;%path%

rem mingw32-make -C calpgm_lite spcat
mingw32-make -B ga-spectroscopy-client CC=gcc

