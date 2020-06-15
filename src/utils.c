#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
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

unsigned int wordCount(string str) {
  unsigned int count = 0;
  // Ignore 1st potential gaps
  while (*str == ' ') str++;
  while (*str != '\n' && *str != 0) {
    count++;
    // Loop through characters
    while (*str != ' ' && *str != '\n' && *str != 0) str++;
    // Ignore gaps
    while (*str == ' ') str++;
  }
  return count;
}

string IgnoreNewLine(string str) {
  str[strlen(str) - 1] = 0;
  return str;
}

string* SplitString(string str,string delimeter) {
  string *array;
  if ((array = (string*)malloc(wordCount(str)*sizeof(string))) == NULL) {
    not_enough_memory();
    return NULL;
  }
  unsigned int index = 0;
  string tmp = strtok(str,delimeter);
  while (tmp != NULL) {
    array[index++] = tmp;
    tmp = strtok(NULL," ");
  }
  return array;
}

// Function to send data to a pipe
void send_data(int fd,char *data,unsigned int dataSize,unsigned int bufferSize) {
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
char *receive_data(int fd,unsigned int bufferSize,boolean toString) {
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

// Function to send data to a socket
//(in sockets we send the size first because the connection will remain for all the chunks we send and we will not read until read() returns 0 because otherwise the connection will block)
void send_data_to_socket(int fd,char *data,unsigned int dataSize,unsigned int bufferSize) {
  unsigned int remBytes = dataSize;
  // Send data size
  uint32_t sizeToSend = htonl(dataSize);
  if (write(fd,&sizeToSend,sizeof(uint32_t)) < 0) {
    return;
  }
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

// Function that reads data from a socker and returns it to a dynamically allocated array
char *receive_data_from_socket(int fd,unsigned int bufferSize,boolean toString) {
  // Read size
  uint32_t dataSize;
  ssize_t bytes_read = 0;
  if ((bytes_read = read(fd,&dataSize,sizeof(uint32_t))) <= 0) {
    // Error check
    if (bytes_read == -1) {
      perror("Error receiving data");
      return NULL;
    }
    // No more data to be read
    return NULL;
  }
  dataSize = ntohl(dataSize);
  uint32_t remBytes = dataSize;
  char buffer[bufferSize];
  char *tmp,*bytestring = NULL;
  unsigned int total_read = 0;
  // Read chunks with specified bufferSize
  while (remBytes >= bufferSize && (bytes_read = read(fd,buffer,bufferSize)) > 0) {
    total_read += bytes_read;
    remBytes -= bytes_read;
    if ((tmp = realloc(bytestring,total_read)) == NULL) {
      not_enough_memory();
      return NULL;
    } else {
      bytestring = tmp;
    }
    memcpy(bytestring + total_read - bytes_read,buffer,bytes_read);
  }
  if (bytes_read == -1) {
    perror("Error receiving data from whoServer");
    return NULL;
  }
  if (remBytes > 0) {
    if ((bytes_read = read(fd,buffer,remBytes)) == -1) {
      perror("Error receiving data from whoServer");
      return NULL;
    }
    total_read += bytes_read;
    remBytes -= bytes_read;
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

// Binds a socket to the given port and machine's ip
int bind_socket(int socket,short port) {
  struct sockaddr_in server;
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = htonl(INADDR_ANY);
  server.sin_port = htons(port);
  return bind(socket,(struct sockaddr*)&server,sizeof(server));
}