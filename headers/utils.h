#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <string.h>

typedef char* string;
typedef char boolean;

#define CEIL(a,b) (((a)+(b)-1)/(b))
#define MAX(A,B) (((A) > (B)) ? (A) : (B))

#define thread_error(s,e) fprintf(stderr , "%s: %s\n", s, strerror(e))

#define MAX_PORT_NUMBER 65535
#define FIFO_PERMS 0666

#define BUFFER_SIZE 4096

#define TRUE 1
#define FALSE 0

int pipe_size();
void not_enough_memory();
string CopyString(string);
int DestroyString(string*);
int stringAppend(string*,string);
// Counts # of words in a string
unsigned int wordCount(string);
string IgnoreNewLine(string);
string* SplitString(string,string);
// Returns true if given string is only a \n feed (used in whoServer's received queries to prevent bugs)
boolean isOnlyNewLine(string);
void send_data(int,char*,unsigned int,unsigned int);
char *receive_data(int,unsigned int,boolean);
void send_data_to_socket(int,char*,unsigned int,unsigned int);
char *receive_data_from_socket(int,unsigned int,boolean);
int digits(unsigned int);
int bind_socket(int socket,short port);

#endif