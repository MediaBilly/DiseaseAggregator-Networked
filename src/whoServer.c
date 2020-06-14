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
#include "../headers/utils.h"

// Definitions for connection pool with circular buffer(Similar to producer-consumer).
// Here we have 1 producer(the main thread) and numThreads consumers
enum ConnectionType {STATISTICS = 0,QUERIES = 1};

#define MAX_STATISTICS_CONNECTIONS 1000
#define MAX_QUERIES_CONNECTIONS 1000

typedef struct {
  int socketDescriptor;
  enum ConnectionType type;
  struct in_addr ip;
  in_port_t port;
} Connection;

typedef struct {
	Connection *connections;
	unsigned int size;
	unsigned int start;
	unsigned int end;
	unsigned int count;
} ConnectionPool;

// Worker info definition
typedef struct {
  struct in_addr ip;
  in_port_t port;
} Worker;

// Global variables
pthread_mutex_t poolMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t pool_nonempty = PTHREAD_COND_INITIALIZER;
pthread_cond_t pool_nonfull = PTHREAD_COND_INITIALIZER;
ConnectionPool connctionPool;
boolean running = TRUE;
// Array of all workers
Worker *workers = NULL;
unsigned int totalWorkers = 0;

/* Connection Pool functions */

void ConnectionPool_Initialize(ConnectionPool* pool,unsigned int size) {
  if ((pool->connections = (Connection*)malloc(size*sizeof(Connection))) == NULL) {
    not_enough_memory();
    exit(EXIT_FAILURE);
  }
  pool->size = size;
  pool->start = pool->end = pool->count = 0;
}

void ConnectionPool_AddConnection(ConnectionPool* pool,Connection conn) {
  pthread_mutex_lock(&poolMutex);
  // Wait until buffer is not full
  while (pool->count == pool->size) {
    pthread_cond_wait(&pool_nonfull,&poolMutex);
  }
  // Add the new connection to the pool
  pool->connections[pool->end] = conn;
  pool->end = (pool->end + 1) % pool->size;
  pool->count++;
  // Signal the consumer threads that we have at least one new connection
  pthread_cond_broadcast(&pool_nonempty);
  pthread_mutex_unlock(&poolMutex);
}

Connection ConnectionPool_GetConnection(ConnectionPool* pool) {
  Connection conn;
  pthread_mutex_lock(&poolMutex);
  // Wait until buffer is not empty
  while (pool->count == 0) {
    pthread_cond_wait(&pool_nonempty,&poolMutex);
  }
  // Obtain connection data
  conn = pool->connections[pool->start];
  pool->start = (pool->start + 1) % pool->size;
  pool->count--;
  // Signal the producer(main thread) that there is space for at least one new connection
  pthread_cond_signal(&pool_nonfull);
  pthread_mutex_unlock(&poolMutex);
  return conn;
}

void ConnectionPool_Destroy(ConnectionPool* pool) {
  // TODO:Close remaining client connections
  free(pool->connections);
}

void usage() {
  fprintf(stderr,"Usage:./whoServer -q queryPortNum -s statisticsPortNum -w numThreads -b bufferSize\n");
}

// Connection consumer
void* client_thread(void *arg) {
  Connection conn;
  while (TRUE) {
    conn = ConnectionPool_GetConnection(&connctionPool);
    switch (conn.type) {
      case QUERIES:
        {
          char* query;
          query = receive_data_from_socket(conn.socketDescriptor,BUFFER_SIZE,TRUE);
          while (query != NULL) {
            // Broadcast to all workers
            // Echo back
            //printf("%s",query);
            send_data_to_socket(conn.socketDescriptor,query,strlen(query),BUFFER_SIZE);
            free(query);
            query = receive_data_from_socket(conn.socketDescriptor,BUFFER_SIZE,TRUE);
          }
        }
        break;
      case STATISTICS:
        {
          char *portData;
          portData = receive_data_from_socket(conn.socketDescriptor,BUFFER_SIZE,FALSE);
          in_port_t port;
          memcpy(&port,portData,sizeof(port));
          free(portData);
          char *statistics;
          statistics = receive_data_from_socket(conn.socketDescriptor,BUFFER_SIZE,TRUE);
          //printf("%s",statistics);
          free(statistics);
          send_data_to_socket(conn.socketDescriptor,"ok",strlen("ok"),BUFFER_SIZE);
          // Add the newly created worker to the worker's list
          if ((workers = (Worker*)realloc(workers,(totalWorkers + 1)*sizeof(Worker))) == NULL) {
            not_enough_memory();
            exit(EXIT_FAILURE);
          }
          workers[totalWorkers].ip = conn.ip;
          workers[totalWorkers++].port = conn.port;
        }
        break;
      default:
        break;
    }
    close(conn.socketDescriptor);
    printf("Closed connection from %s:%d\n",inet_ntoa(conn.ip),(int)ntohs(conn.port));
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
  unsigned short queryPortNum,statisticsPortNum;
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
  // Create a connection pool (implemented with circular buffer as producer-consumer problem)
  ConnectionPool_Initialize(&connctionPool,bufferSize);
  // Create numThreads to handle connections
  pthread_t threads[numThreads];
  unsigned int i;
  int threadErr;
  for (i = 0;i < numThreads;i++) {
    if ((threadErr = pthread_create(&threads[i],NULL,client_thread,NULL))) {
      thread_error("whoServer thread creation error",threadErr);
      return 1;
    }
  }
  // Create socket taht listens to worker statistics
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
  //printf("Listening for incoming whoClients on port:%d\n",queryPortNum);
  int clientSocket;
  struct sockaddr_in clientAddress;
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
        // Get client's ip address
        printf("New statistics connection accepted from %s:%d\n",inet_ntoa(clientAddress.sin_addr),(int)ntohs(clientAddress.sin_port));
        // Add the new connection to the circular buffer pool
        Connection newConn;
        newConn.socketDescriptor = clientSocket;
        newConn.type = STATISTICS;
        newConn.ip = clientAddress.sin_addr;
        newConn.port = clientAddress.sin_port;
        ConnectionPool_AddConnection(&connctionPool,newConn);
      } 
      if (FD_ISSET(queriesSocket,&listeningSockets)) {
        if ((clientSocket = accept(queriesSocket,(struct sockaddr*)&clientAddress,&clientAddressLength)) < 0) {
          perror("Failed to accept incoming connection");
          return 1;
        }
        // Get client's ip address
        printf("New queries connection accepted from %s:%d\n",inet_ntoa(clientAddress.sin_addr),(int)ntohs(clientAddress.sin_port));
        // Add the new connection to the circular buffer pool
        Connection newConn;
        newConn.socketDescriptor = clientSocket;
        newConn.type = QUERIES;
        newConn.ip = clientAddress.sin_addr;
        newConn.port = clientAddress.sin_port;
        ConnectionPool_AddConnection(&connctionPool,newConn);
      }
    } 
  }
  // Close listening sockets
  close(queriesSocket);
  close(statisticsSocket);
  // Terminate the threads
  int threadStatus;
  for (i = 0;i < numThreads;i++) {
    pthread_cancel(threads[i]);
    pthread_mutex_unlock(&poolMutex);
    pthread_join(threads[i],(void**)&threadStatus);
  }
  // Deallocate connection pool
  ConnectionPool_Destroy(&connctionPool);
  // Deallocate workers
  free(workers);
  return 0;
}
