/*
 * write template functions that are guaranteed to read and write the
 * number of bytes desired
 */

#ifndef fancyRW_h
#define fancyRW_h

#include <sys/types.h>
#include<sys/socket.h>
#include<unistd.h> //for read/write
#include<netdb.h>
#include<string.h> //for memset
#include<stdio.h> //for fprintf, stderr, etc.
#include<stdlib.h> //for exit
#include<errno.h>

template<typename T>
int READ(int fd, T* obj_ptr, int count)
{
  int bytes_left = count, bytes_read = 0;
  char* addr=(char*)obj_ptr;
  //loop. Read repeatedly until count bytes are read in
  while(bytes_left > 0){
    bytes_read = read(fd, addr, bytes_left);

    if (bytes_read == -1 && errno == EINTR){ // recoverable error
      bytes_read = 0;
      continue;
    }
    else if(bytes_read == -1){ // unrecoverable error
      return -1;
    }
    else if(bytes_read == 0){ // EOF
      break;
    }
  	bytes_left -= bytes_read;
  	addr += bytes_read;
  }
  return count; // returning total bytes read
}

template<typename T>
int WRITE(int fd, T* obj_ptr, int count)
{
  int bytes_left = count, bytes_written = 0;
  char* addr=(char*)obj_ptr;
  //loop. Write repeatedly until count bytes are written out
  while(bytes_left > 0){
    bytes_written = write(fd, addr, bytes_left);

    if (bytes_written == -1 && errno == EINTR){ // recoverable error
      bytes_written = 0;
      continue;
    }
    else if(bytes_written == -1){ // unrecoverable error
      return -1;
    }
    bytes_left -= bytes_written;
    addr += bytes_written;
  }
  return count; // returning total bytes written
}

#endif
