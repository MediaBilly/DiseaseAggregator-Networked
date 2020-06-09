#ifndef UTILS_H
#define UTILS_H

typedef char* string;
typedef char boolean;

#define CEIL(a,b) (((a)+(b)-1)/(b))

#define MAX_PORT_NUMBER 65535
#define FIFO_PERMS 0666

#define TRUE 1
#define FALSE 0

int pipe_size();
void not_enough_memory();
string CopyString(string);
int DestroyString(string*);
void send_data_to_pipe(int,char*,unsigned int,unsigned int);
char *receive_data_from_pipe(int,unsigned int,boolean);

#endif