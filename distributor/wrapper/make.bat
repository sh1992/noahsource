@path c:\mingw\bin;c:\mingw\msys\1.0\bin;%path%
windres distclient.rc -O coff -o distclient.res
gcc -o distclient.exe -mwindows wrapper.c distclient.res
strip distclient.exe
