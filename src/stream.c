/*
 * stream.c
 *
 * All rights reserved. Copyright (C) 1996 by NARITA Tomio.
 * $Id: stream.c,v 1.5 2003/11/13 03:08:19 nrt Exp $
 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef UNIX
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#endif /* UNIX */

#ifdef MSDOS
#include <dos.h>
#endif /* MSDOS */

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#endif /* _WIN32 */

#include <import.h>
#include <uty.h>
#include <begin.h>
#include <command.h>
#include "stream.h"

private byte *gz_filter = "zcat";
private byte *bz2_filter = "bzcat";
private byte *xz_filter = "xzcat";

private stream_t *StreamAlloc()
{
  stream_t *st;

  st = (stream_t *)Malloc( sizeof( stream_t ) );

  st->fp  = NULL;
  st->sp  = NULL;
  st->pid = -1;

  return st;
}

#ifdef _WIN32
/* MSVC's tmpfile() creates a file on a root directory.
 * It might fail on Windows Vista or later.
 * Implement our own tmpfile(). */
private FILE *mytmpfile( void )
{
  TCHAR TempFileName[ MAX_PATH ];
  TCHAR TempPath[ MAX_PATH ];
  DWORD ret;
  HANDLE hFile;
  int fd;
  FILE *fp;

  ret = GetTempPath( MAX_PATH, TempPath );
  if( ret > MAX_PATH || ret == 0 )
    return NULL;

  ret = GetTempFileName( TempPath, TEXT( "lv" ), 0, TempFileName );
  if( ret == 0 )
    return NULL;

  hFile = CreateFile( TempFileName, GENERIC_READ | GENERIC_WRITE,
      0, NULL, CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, NULL );
  if( hFile == INVALID_HANDLE_VALUE )
    return NULL;

  fd = _open_osfhandle( (intptr_t)hFile, 0 );
  if( fd == -1 ){
    CloseHandle( hFile );
    return NULL;
  }

  fp = _fdopen( fd, "w+b" );
  if( fp == NULL ){
    _close( fd );
    return NULL;
  }
  return fp;
}
#define tmpfile()   mytmpfile()
#endif /* _WIN32 */

public stream_t *StreamOpen( byte *file )
{
  stream_t *st;
  byte *exts, *filter = NULL;

  if( access( file, 0 ) ){
    perror( file );
    return NULL;
  }

  st = StreamAlloc();

  if( !strcmp( "AUTO", filter_program ) ){
    if( NULL != (exts = Exts( file )) ){
      if( !strcmp( "gz", exts ) || !strcmp( "GZ", exts )
	  || !strcmp( "z", exts ) || !strcmp( "Z", exts ) )
	filter = gz_filter;
      else if( !strcmp( "bz2", exts ) || !strcmp( "BZ2", exts ) )
	filter = bz2_filter;
      else if( !strcmp( "xz", exts ) || !strcmp( "XZ", exts )
	  || !strcmp( "lzma", exts ) || !strcmp( "LZMA", exts ) )
	filter = xz_filter;
    }
  } else if( !strcmp( "NONE", filter_program ) ){
    filter = NULL;
  } else {
    filter = filter_program;
  }
  if( NULL != filter ){
    /*
     * zcat, bzcat or xzcat
     */

#define COM_SIZE 128
#define ARG_SIZE 64
    int argc;
    byte *ptr, *argv[ ARG_SIZE ];
    byte com[ COM_SIZE ];

    if( NULL == (st->fp = (FILE *)tmpfile()) )
      perror( "temporary file" ), exit( -1 );

    if( strlen( filter ) + 1 > COM_SIZE )
      errno = E2BIG, perror( filter ), exit( -1 );

    strcpy( com, filter );

    ptr = com;
    argc = 0;
    while( 0x00 != *ptr && argc < ARG_SIZE - 2 ){
      argv[ argc ] = ptr;
      while( ' ' != *ptr && 0x00 != *ptr )
	ptr++;
      if( 0x00 != *ptr ){
	*ptr++ = 0x00;
	while( ' ' == *ptr && 0x00 != *ptr )
	  ptr++;
      }
      argc++;
    }
    argv[ argc++ ] = file;
    argv[ argc ] = NULL;

#ifdef MSDOS
    { int sout;

      sout = dup( 1 );
      close( 1 );
      dup( fileno( st->fp ) );
      if( 0 > spawnvp( 0, argv[0], argv ) )
	FatalErrorOccurred();
      close( 1 );
      dup( sout );
      rewind( st->fp );

      return st;
    }
#endif /* MSDOS */

#ifdef _WIN32
    { char buf[1024];

      sprintf(buf, "%s \"%s\"", filter, file);
      if( NULL == (st->sp = _popen( buf, "rb" )) )
	perror( "fdopen" ), exit( -1 );

      return st;
    }
#endif /* _WIN32 */

#ifdef UNIX
    { int fds[ 2 ], pid;

      if( 0 > pipe( fds ) )
	perror( "pipe" ), exit( -1 );

      switch( pid = fork() ){
      case -1:
	perror( "fork" ), exit( -1 );
      case 0:
	/*
	 * child process
	 */
	close( fds[ 0 ] );
	close( 1 );
	dup( fds[ 1 ] );
	if( 0 > execvp( argv[0], (char **)argv ) )
	  perror( filter ), exit( -1 );
	/*
	 * never reach here
	 */
      default:
	/*
	 * parent process
	 */
	st->pid = pid;
	close( fds[ 1 ] );
	if( NULL == (st->sp = fdopen( fds[ 0 ], "r" )) )
	  perror( "fdopen" ), exit( -1 );

	return st;
      }
    }
#endif /* UNIX */
  }

  if( NULL == (st->fp = fopen( file, "rb" )) ){
    perror( file );
    return NULL;
  }

  return st;
}

private void StdinDuplicationFailed()
{
  fprintf( stderr, "lv: stdin duplication failed\n" );
  exit( -1 );
}

public stream_t *StreamReconnectStdin()
{
  stream_t *st;
#if defined( UNIX ) || defined( _WIN32 )
  struct stat sbuf;
#endif

  st = StreamAlloc();

#ifdef MSDOS
  if( NULL == (st->fp = fdopen( dup( 0 ), "rb" )) )
    StdinDuplicationFailed();
  close( 0 );
  dup( 1 );
#endif /* MSDOS */

#ifdef _WIN32
  fstat( 0, &sbuf );
  if( S_IFREG == ( sbuf.st_mode & S_IFMT ) ){
    /* regular */
    if( NULL == (st->fp = fdopen( dup( 0 ), "rb" )) )
      StdinDuplicationFailed();
  } else {
    /* socket */
    if( NULL == (st->fp = (FILE *)tmpfile()) )
      perror( "temporary file" ), exit( -1 );
    if( NULL == (st->sp = fdopen( dup( 0 ), "rb" )) )
      StdinDuplicationFailed();
  }
  close( 0 );
#endif /* _WIN32 */

#ifdef UNIX
  fstat( 0, &sbuf );
  if( S_IFREG == ( sbuf.st_mode & S_IFMT ) ){
    /* regular */
    if( NULL == (st->fp = fdopen( dup( 0 ), "r" )) )
      StdinDuplicationFailed();
  } else {
    /* socket */
    if( NULL == (st->fp = (FILE *)tmpfile()) )
      perror( "temporary file" ), exit( -1 );
    if( NULL == (st->sp = fdopen( dup( 0 ), "r" )) )
      StdinDuplicationFailed();
  }
  close( 0 );
  if( IsAtty( 1 ) && 0 != open( "/dev/tty", O_RDONLY ) )
    perror( "/dev/tty" ), exit( -1 );
#endif /* UNIX */

  return st;
}

public boolean_t StreamClose( stream_t *st )
{
  fclose( st->fp );
  if( st->sp )
    fclose( st->sp ); // FIXME: Use _pclose if sp is opened by _popen.

  free( st );

  return TRUE;
}
