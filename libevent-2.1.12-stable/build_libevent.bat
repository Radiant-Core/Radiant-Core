
@echo off
echo Building libevent...

# Set compiler
set CC=gcc
set CFLAGS=-O2 -DHAVE_CONFIG_H -I./include -I./WIN32-Code -I.

# Compile source files
echo Compiling event.c
%CC% %CFLAGS% -c event.c -o event.o

echo Compiling buffer.c
%CC% %CFLAGS% -c buffer.c -o buffer.o

echo Compiling evbuffer.c
%CC% %CFLAGS% -c evbuffer.c -o evbuffer.o

echo Compiling log.c
%CC% %CFLAGS% -c log.c -o log.o

echo Compiling evutil.c
%CC% %CFLAGS% -c evutil.c -o evutil.o

echo Compiling strlcpy.c
%CC% %CFLAGS% -c strlcpy.c -o strlcpy.o

echo Creating library
ar rcs libevent.a event.o buffer.o evbuffer.o log.o evutil.o strlcpy.o

echo Done!
