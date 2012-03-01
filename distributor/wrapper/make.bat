@echo off
rem
rem make.bat - Compile distclient wrapper using MinGW
rem

path c:\mingw\bin;c:\mingw\msys\1.0\bin;%path%

mingw32-make distclient.exe CC=gcc
