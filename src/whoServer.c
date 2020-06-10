#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include "../headers/utils.h"

void usage() {
  fprintf(stderr,"Usage:./whoServer -q queryPortNum -s statisticsPortNum -w numThreads -b bufferSize\n");
}

int main(int argc, char const *argv[]) {
  // Check usage
  if (argc != 9) {
    usage();
    return 1;
  }
  short queryPortNum,statisticsPortNum;
  unsigned int numThreads,bufferSize;
  // Read queryPortNum
  if (!strcmp(argv[1],"-q")) {
    queryPortNum = (short)atoi(argv[2]);
  } else {
    usage();
    return 1;
  }
  // Read statisticsPortNum
  if (!strcmp(argv[3],"-s")) {
    statisticsPortNum = (short)atoi(argv[4]);
  } else {
    usage();
    return 1;
  }
  // Read numThreads
  if (!strcmp(argv[5],"-w")) {
    numThreads = atoi(argv[6]);
  } else {
    usage();
    return 1;
  }
  // Read bufferSize
  if (!strcmp(argv[7],"-b")) {
    bufferSize = atoi(argv[8]);
  } else {
    usage();
    return 1;
  }
  // Create socket that listens to queries
  int queriesSocket;
  if ((queriesSocket = socket(AF_INET,SOCK_STREAM,0)) == -1) {
    perror("Failed to create queries socket:");
    return 1;
  }
  // Bind that socket 
  if (bind_socket(queriesSocket,queryPortNum) < 0) {
    perror("Queries socket bind failed:");
    return 1;
  }
  // Start listening for whoClients incoming connections
  if (listen(queriesSocket,queryPortNum) < 0) {
    perror("Queries socket failed to start listening to incoming whoClients:");
    return 1;
  }
  printf("Listening for incoming whoClients on port:%d\n",queryPortNum);
  int clientSocket;
  struct sockaddr_in clientAddress;
  socklen_t clientAddressLength = 0;
  while (TRUE) {
    if ((clientSocket = accept(queriesSocket,(struct sockaddr*)&clientAddress,&clientAddressLength)) < 0) {
      perror("Failed to accept incoming connection:");
      return 1;
    }
    printf("New connection accepted!\n");
    // Receive data(temporary sequentially until circularBuffer technique (similar to readers & writers problem) is implemented)
    char *receivedData = receive_data_from_socket(clientSocket,BUFFER_SIZE,TRUE);
    while (receivedData != NULL) {
      // Echo back
      printf("%s",receivedData);
      send_data_to_socket(clientSocket,receivedData,strlen(receivedData),BUFFER_SIZE);
      free(receivedData);
      receivedData = receive_data_from_socket(clientSocket,BUFFER_SIZE,TRUE);
    }
    printf("Closing connection\n");
    close(clientSocket);
  }
  return 0;
}
