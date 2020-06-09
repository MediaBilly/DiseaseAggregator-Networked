#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../headers/utils.h"

int pipe_size() {
  int p[2];
  if (pipe(p) == -1) {
    perror("pipe");
    exit(1);
  }
  int pipe_size = fpathconf(p[0],_PC_PIPE_BUF);
  close(p[0]);
  close(p[1]);
  return pipe_size;
}

void not_enough_memory() {
  perror("malloc");
}

string CopyString(string str) {
  string ret = NULL;
  if ((ret = (string)malloc(strlen(str) + 1)) != NULL) {
    strcpy(ret,str);
  } else {
    not_enough_memory();
  }
  return ret;
}

int DestroyString(string *str) {
  if (*str != NULL) {
    free(*str);
    *str = NULL;
    return TRUE;
  } else {
    return FALSE;
  }
}

// Appends substr to str's end
int stringAppend(string *str,string substr) {
  if ((*str = realloc(*str,strlen(*str) + strlen(substr) + 1)) == NULL) {
      printf("Not enough memory.\n");
      return 0;
  }
  strcpy(*str + strlen(*str),substr);
  return 1;
}

// Function to send data to a pipe
void send_data_to_pipe(int fd,char *data,unsigned int dataSize,unsigned int bufferSize) {
  unsigned int remBytes = dataSize;
  // Send first chunks with specified bufferSize
  while (remBytes >= bufferSize && write(fd,data,bufferSize) > 0) {
    remBytes -= bufferSize;
    data += bufferSize;
  }
  // Send last chunk with size < bufferSize
  if (remBytes > 0) {
    write(fd,data,remBytes);
  }
}

// Function that reads data from a pipe and returns it to a dynamically allocated array
char *receive_data_from_pipe(int fd,unsigned int bufferSize,boolean toString) {
  ssize_t bytes_read = 0;
  char buffer[bufferSize];
  char *tmp,*bytestring = NULL;
  unsigned int total_read = 0;
  // Read chunks with specified bufferSize
  while ((bytes_read = read(fd,buffer,bufferSize)) > 0) {
    total_read += bytes_read;
    if ((tmp = realloc(bytestring,total_read)) == NULL) {
      not_enough_memory();
      return NULL;
    } else {
      bytestring = tmp;
    }
    memcpy(bytestring + total_read - bytes_read,buffer,bytes_read);
  }
  if (total_read == 0) {
    return NULL;
  }
  // Set \0 for string ending if needed
  if (toString) {
    bytestring = realloc(bytestring,total_read + 1);
    bytestring[total_read] = 0;
  }
  return bytestring;
}

int digits(unsigned int num) {
  if (num == 0) {
    return 1;
  }
  int count = 0;
  while (num != 0) {
    count++;
    num/=10;
  }
  return count;
}