#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include "../headers/connectionPool.h"
#include "../headers/utils.h"

#define MAX_STATISTICS_CONNECTIONS 1000
#define MAX_QUERIES_CONNECTIONS 1000

// Worker info definition
typedef struct {
  struct in_addr ip;
  in_port_t port;
  int socketFd;
} Worker;

// Global variables
ConnectionPool connectionPool;
boolean running = TRUE;
// Array of all workers
Worker *workers = NULL;
unsigned int totalWorkers = 0;
unsigned short queryPortNum,statisticsPortNum;
unsigned int numThreads,bufferSize;
static pthread_mutex_t workersMutex = PTHREAD_MUTEX_INITIALIZER;

void usage() {
  fprintf(stderr,"Usage:./whoServer -q queryPortNum -s statisticsPortNum -w numThreads -b bufferSize\n");
}

// Connection consumer
void* client_thread(void *arg) {
  Connection conn;
  while (running) {
    conn = ConnectionPool_GetConnection(&connectionPool);
    switch (conn.type) {
      case QUERIES:
        {
          char* query;
          query = receive_data_from_socket(conn.socketDescriptor,BUFFER_SIZE,TRUE);
          printf("whoServer received query:%s\n",query);
          fd_set workerFdSet;
          while (query != NULL) {
            // Check if \n given from client and if so give null answer
            if (isOnlyNewLine(query)) {
              free(query);
              send_data_to_socket(conn.socketDescriptor,"",strlen(""),BUFFER_SIZE);
              query = receive_data_from_socket(conn.socketDescriptor,BUFFER_SIZE,TRUE);
              continue;
            }
            IgnoreNewLine(query);
            // Broadcast to all workers
            FD_ZERO(&workerFdSet);
            int i,connectedWorkers = 0,maxFd = -1;
            string answer = NULL;
            // Send the query to all the workers
            pthread_mutex_lock(&workersMutex);
            for (i = 0;i < totalWorkers;i++) {
              int wSock = workers[i].socketFd;
              // Send him the query
              if (send_data_to_socket(wSock,query,strlen(query),BUFFER_SIZE)) {
                printf("Thread %ld connected to worker %s:%d for query:%s\n",pthread_self(),inet_ntoa(workers[i].ip),workers[i].port,query);
              }
              FD_SET(wSock,&workerFdSet);
              maxFd = MAX(maxFd,wSock);
              connectedWorkers++;
            }
            // Wait for them to answer
            fd_set readyFdSet;
            unsigned int cmdArgc = wordCount(query);
            string *args = SplitString(query," ");
            string command = args[0];
            unsigned int diseaseFrequencySum = 0;
            while (connectedWorkers > 0) {
              //printf("*%d*\n",connectedWorkers);
              // Copy fd set because fd set functions are destructicve
              readyFdSet = workerFdSet;
              // Wait for at least one worker to answer
              if (select(FD_SETSIZE,&readyFdSet,NULL,NULL,NULL) < 0) {
                perror("select");
                exit(EXIT_FAILURE);
              }
              for (i = 0;i < FD_SETSIZE;i++) {
                if (FD_ISSET(i,&readyFdSet)) {
                  char *workerAns;
                  if ((workerAns = receive_data_from_socket(i,BUFFER_SIZE,TRUE)) != NULL) {
                    // Handle answer
                    if (!strcmp(command,"/diseaseFrequency")) {
                      diseaseFrequencySum += atoi(workerAns);
                    } else if (!strcmp(command,"/topk-AgeRanges") || !strcmp(command,"/searchPatientRecord") || !strcmp(command,"/numPatientAdmissions") || !strcmp(command,"/numPatientDischarges")) {
                      if (strcmp(workerAns,"nf")) {
                        stringAppend(&answer,workerAns);
                      }
                    } else {
                      if (answer == NULL) {
                        stringAppend(&answer,workerAns);
                      }
                    }
                    free(workerAns);
                    FD_CLR(i,&workerFdSet);
                    connectedWorkers--;
                  }
                }
              }
            }
            pthread_mutex_unlock(&workersMutex);
            if (!strcmp(command,"/diseaseFrequency")) {
              char ans[10];
              sprintf(ans,"%u\n",diseaseFrequencySum);
              stringAppend(&answer,ans);
            }
            if (answer != NULL) {
              send_data_to_socket(conn.socketDescriptor,answer,strlen(answer),BUFFER_SIZE);
            } else {
              if (!strcmp(command,"/searchPatientRecord")) {
                send_data_to_socket(conn.socketDescriptor,"Not found\n",strlen("Not found\n"),BUFFER_SIZE);
              } else if (!strcmp(command,"/topk-AgeRanges")) {
                send_data_to_socket(conn.socketDescriptor,"Country or disease not found",strlen("Country or disease not found"),BUFFER_SIZE);
              } else if ((!strcmp(command,"/numPatientAdmissions") || !strcmp(command,"/numPatientDischarges")) && cmdArgc == 5) {
                char ans[strlen(args[4]) + strlen(" 0")];
                sprintf(ans,"%s 0\n",args[4]);
                send_data_to_socket(conn.socketDescriptor,ans,strlen(ans),BUFFER_SIZE);
              }
            }
            free(args);
            free(answer);
            free(query);
            query = receive_data_from_socket(conn.socketDescriptor,BUFFER_SIZE,TRUE);
          }
        }
        break;
      case STATISTICS:
        {
          char *portData;
          portData = receive_data_from_socket(conn.socketDescriptor,BUFFER_SIZE,TRUE);
          in_port_t port = atoi(portData);
          free(portData);
          char *statistics;
          statistics = receive_data_from_socket(conn.socketDescriptor,BUFFER_SIZE,TRUE);
          //printf("%s",statistics);
          free(statistics);
          char numThreadsStr[digits(numThreads) + 1];
          sprintf(numThreadsStr,"%u",numThreads);
          // Add the newly created worker to the worker's list
          if ((workers = (Worker*)realloc(workers,(totalWorkers + 1)*sizeof(Worker))) == NULL) {
            not_enough_memory();
            exit(EXIT_FAILURE);
          }
          workers[totalWorkers].ip = conn.ip;
          workers[totalWorkers].port = port;
          // Establish connection with that worker
          int wSock;
          if ((wSock = socket(AF_INET,SOCK_STREAM,0)) < 0) {
            perror("whoServer could not create socket to communicate with a worker");
            continue;
          }
          struct sockaddr_in workerAddress;
          workerAddress.sin_family = AF_INET;
          workerAddress.sin_addr = workers[totalWorkers].ip;
          workerAddress.sin_port = htons(workers[totalWorkers].port);
          if (connect(wSock,(struct sockaddr*)&workerAddress,sizeof(workerAddress)) < 0) {
            perror("whoServer could not connect to a worker");
            close(wSock);
            continue;
          }
          workers[totalWorkers++].socketFd = wSock;
        }
        break;
      default:
        break;
    }
    close(conn.socketDescriptor);
    printf("Closed connection from %s:%d\n",inet_ntoa(conn.ip),conn.port);
  }
  pthread_exit((void*)EXIT_SUCCESS);
}

void finish_execution(int signum) {
  signal(SIGINT,finish_execution);
  signal(SIGQUIT,finish_execution);
  running = FALSE;
}

int main(int argc, char const *argv[]) {
  // Set max open files limit
  struct rlimit max_open_files;
  getrlimit(RLIMIT_NOFILE,&max_open_files);
  max_open_files.rlim_cur = max_open_files.rlim_max;
  setrlimit(RLIMIT_NOFILE,&max_open_files);
  // Register signal handlers that terminate the server
  signal(SIGINT,finish_execution);
  signal(SIGQUIT,finish_execution);
  // Check usage
  if (argc != 9) {
    usage();
    return 1;
  }
  // Read queryPortNum
  if (!strcmp(argv[1],"-q")) {
    queryPortNum = (short)atoi(argv[2]);
  } else {
    usage();
    return 1;
  }
  // Read statisticsPortNum
  if (!strcmp(argv[3],"-s")) {
    statisticsPortNum = (unsigned short)atoi(argv[4]);
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
  // Create socket that listens to worker statistics
  int statisticsSocket;
  if ((statisticsSocket = socket(AF_INET,SOCK_STREAM |SOCK_NONBLOCK,0)) == -1) {
    perror("Failed to create statistics socket");
    return 1;
  }
  // Bind that socket 
  if (bind_socket(statisticsSocket,statisticsPortNum) < 0) {
    perror("Statistics socket bind failed");
    return 1;
  }
  // Start listening for whoClients incoming connections
  if (listen(statisticsSocket,max_open_files.rlim_cur) < 0) {
    perror("Statistics socket failed to start listening to incoming whoClients");
    return 1;
  }
  // Create socket that listens to queries
  int queriesSocket;
  if ((queriesSocket = socket(AF_INET,SOCK_STREAM | SOCK_NONBLOCK,0)) == -1) {
    perror("Failed to create queries socket");
    return 1;
  }
  // Bind that socket 
  if (bind_socket(queriesSocket,queryPortNum) < 0) {
    perror("Queries socket bind failed");
    return 1;
  }
  // Start listening for whoClients incoming connections
  if (listen(queriesSocket,max_open_files.rlim_cur) < 0) {
    perror("Queries socket failed to start listening to incoming whoClients");
    return 1;
  }
  // Create a connection pool (implemented with circular buffer as producer-consumer problem)
  ConnectionPool_Initialize(&connectionPool,bufferSize,numThreads,client_thread,NULL);
  // Create numThreads to handle connections
  int clientSocket;
  struct sockaddr_in clientAddress;
  memset(&clientAddress,0,sizeof(clientAddress));
  socklen_t clientAddressLength = 0;
  fd_set listeningSockets;
  printf("Listening for incoming workers to send statistics on port:%d\n",statisticsPortNum);
  printf("Listening for incoming whoClients on port:%d\n",queryPortNum);
  while (running) {
    FD_ZERO(&listeningSockets);
    FD_SET(statisticsSocket,&listeningSockets);
    FD_SET(queriesSocket,&listeningSockets);
    if (select(MAX(statisticsSocket,queriesSocket) + 1,&listeningSockets,NULL,NULL,NULL) != -1) {
      if (FD_ISSET(statisticsSocket,&listeningSockets)) {
        if ((clientSocket = accept(statisticsSocket,(struct sockaddr*)&clientAddress,&clientAddressLength)) < 0) {
          perror("Failed to accept incoming connection");
          return 1;
        }
        // Add the new connection to the circular buffer pool
        Connection newConn;
        struct sockaddr_in acceptedAddress;
        memset(&acceptedAddress,0,sizeof(acceptedAddress));
        socklen_t len = sizeof(acceptedAddress);
        // Get client's ip address
        if (getsockname(clientSocket,(struct sockaddr*)&acceptedAddress,&len) != -1) {
          newConn.socketDescriptor = clientSocket;
          newConn.type = STATISTICS;
          newConn.ip = acceptedAddress.sin_addr;
          newConn.port = ntohs(acceptedAddress.sin_port);
          ConnectionPool_AddConnection(&connectionPool,newConn);
          printf("New statistics connection accepted from %s:%d\n",inet_ntoa(acceptedAddress.sin_addr),(int)ntohs(acceptedAddress.sin_port));
        } else {
          perror("getsockname");
          close(clientSocket);
        }
      } 
      if (FD_ISSET(queriesSocket,&listeningSockets)) {
        if ((clientSocket = accept(queriesSocket,(struct sockaddr*)&clientAddress,&clientAddressLength)) < 0) {
          perror("Failed to accept incoming connection");
          return 1;
        }
        // Add the new connection to the circular buffer pool
        Connection newConn;
        struct sockaddr_in acceptedAddress;
        memset(&acceptedAddress,0,sizeof(acceptedAddress));
        socklen_t len = sizeof(acceptedAddress);
        // Get client's ip address
        if (getsockname(clientSocket,(struct sockaddr*)&acceptedAddress,&len) != -1) {
          newConn.socketDescriptor = clientSocket;
          newConn.type = QUERIES;
          newConn.ip = acceptedAddress.sin_addr;
          newConn.port = ntohs(acceptedAddress.sin_port);
          ConnectionPool_AddConnection(&connectionPool,newConn);
          printf("New queries connection accepted from %s:%d\n",inet_ntoa(acceptedAddress.sin_addr),(int)ntohs(acceptedAddress.sin_port));
        } else {
          perror("getsockname");
          close(clientSocket);
        }
      }
    } 
  }
  // Close listening sockets
  close(queriesSocket);
  close(statisticsSocket);
  // Deallocate connection pool
  ConnectionPool_Destroy(&connectionPool);
  // Disconnect from all the workers
  unsigned int i;
  for (i = 0;i < totalWorkers;i++) {
    close(workers[i].socketFd);
  }
  // Deallocate workers
  free(workers);
  return 0;
}
