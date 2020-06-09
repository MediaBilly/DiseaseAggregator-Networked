#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include "../headers/list.h"
#include "../headers/utils.h"

// Declare global variables
List countriesList;
boolean running = TRUE;

void usage() {
  fprintf(stderr,"Usage:./worker receiver_fifo input_dir bufferSize\n");
}

void stop_execution(int signum) {
  running = FALSE;
}

int main(int argc, char const *argv[]) {
  // Register signal handlers that handle execution termination
  signal(SIGINT,stop_execution);
  signal(SIGQUIT,stop_execution);
  // Usage check
  if (argc != 4) {
    usage();
    return 1;
  }
  string receiver_fifo,input_dir,serverIp,serverPort;
  unsigned int bufferSize;
  // Read receiver_fifo(pipe that receives data from master process)
  receiver_fifo = (string)argv[1];
  // Read input_dir
  input_dir = (string)argv[2];
  // Read bufferSize;
  bufferSize = atoi(argv[3]);
  // Open receiver fifo to read data from master
  int receiverFd = open(receiver_fifo,O_RDONLY);
  string masterData = receive_data_from_pipe(receiverFd,bufferSize,TRUE);
  close(receiverFd);
  if (masterData == NULL) {
    return 1;
  }
  // Read server ip
  serverIp = strtok(masterData,"\n");
  // Read server port
  serverPort = strtok(NULL,"\n");
  // Initialize countries list
  if (!List_Initialize(&countriesList)) {
    return 1;
  }
  printf("LISTENING on %s:%s\n",serverIp,serverPort);
  // Read the countries sent by diseaseAggregator;
  string country = strtok(NULL,"\n");
  while (country != NULL) {
    List_Insert(countriesList,country);
    country = strtok(NULL,"\n");
  }
  // Free memory needed for receiving the countries
  free(masterData);
  ListIterator countriesIt = List_CreateIterator(countriesList);
  while (countriesIt != NULL) {
    printf("%d %s\n",getpid(),ListIterator_GetValue(countriesIt));
    ListIterator_MoveToNext(&countriesIt);
  }
  while(running);
  List_Destroy(&countriesList);
  return 0;
}
