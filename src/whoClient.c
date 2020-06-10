#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include "../headers/list.h"
#include "../headers/utils.h"

// Global variables (needed by all threads)
struct sockaddr_in serverAddress;
struct hostent *serverEntry;
string servIp;
short servPort;
boolean threadsCreated = FALSE;

static pthread_mutex_t threadsMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t threadsCreatedCond = PTHREAD_COND_INITIALIZER;

void usage() {
  fprintf(stderr,"Usage:./whoClient -q queryFile -w numThreads -sp servPort -sip servIP\n");
}

void* thread_function(void *arg) {
  // Wait until all the threads are created
  pthread_mutex_lock(&threadsMutex);
  while (!threadsCreated) {
    pthread_cond_wait(&threadsCreatedCond,&threadsMutex);
  }
  pthread_mutex_unlock(&threadsMutex);
  // Get queries list
  List queries = (List)arg;
  // Create an iterator for it
  ListIterator queriesIt = List_CreateIterator(queries);
  // Connect to whoServer
  int serverSocket;
  if ((serverSocket = socket(AF_INET,SOCK_STREAM,0)) < 0) {
    perror("whoClient socket creation error");
    pthread_exit((void*)EXIT_FAILURE);
  }
  if (connect(serverSocket,(struct sockaddr*)&serverAddress,sizeof(serverAddress)) < 0) {
    perror("whoClient could not connect to whoServer");
    pthread_exit((void*)EXIT_FAILURE);
  }
  // Send queries to whoServer
  while (queriesIt != NULL) {
    send_data_to_socket(serverSocket,ListIterator_GetValue(queriesIt),strlen(ListIterator_GetValue(queriesIt)),BUFFER_SIZE);
    printf("CLIENT SENDING %s\n",ListIterator_GetValue(queriesIt));
    // Get the answer from whoServer
    string answer = receive_data_from_socket(serverSocket,BUFFER_SIZE,TRUE);
    printf("%ld received %s\n",pthread_self(),answer);
    free(answer);
    ListIterator_MoveToNext(&queriesIt);
  }
  // Disconnect from whoServer
  close(serverSocket);
  pthread_exit((void*)0);
}

int main(int argc, char const *argv[]) {
  // Usage check
  if (argc != 9) {
    usage();
    return 1;
  }
  string queryFile;
  unsigned int numThreads;
  // Read queryFile
  if (!strcmp(argv[1],"-q")) {
    queryFile = (string)argv[2];
  } else {
    usage();
    return 1;
  }
  // Read numThreads
  if (!strcmp(argv[3],"-w")) {
    numThreads = atoi(argv[4]);
  } else {
    usage();
    return 1;
  }
  // Read servPort
  if (!strcmp(argv[5],"-sp")) {
    servPort = (short)atoi(argv[6]);
  } else {
    usage();
    return 1;
  }
  // Read servIP
  if (!strcmp(argv[7],"-sip")) {
    servIp = (string)argv[8];
  } else {
    usage();
    return 1;
  }
  // Find whoServer address
  if ((serverEntry = gethostbyname(servIp)) == NULL) {
    herror("whoClient failed to resolve whoServer's hostname");
    pthread_exit((void*)EXIT_FAILURE);
  }
  serverAddress.sin_family = AF_INET;
  memcpy(&serverAddress.sin_addr,serverEntry->h_addr,serverEntry->h_length);
  serverAddress.sin_port = htons(servPort);
  // Open queries file
  FILE *queryFileHandle;
  if ((queryFileHandle = fopen(queryFile,"r")) == NULL) {
    perror("Could not open queryFile:");
    return 1;
  }
  // Create numThreads lists one for each thread that will keep the queries to send to whoServer
  List threadQueries[numThreads];
  unsigned int i,curThread = 0;
  for (i = 0;i < numThreads;i++) {
    if (!List_Initialize(&threadQueries[i])) {
      fclose(queryFileHandle);
      return 1;
    }
  }
  // Distribute the queries to the threads
  string line = NULL;
  size_t len = 0;
  while (getline(&line,&len,queryFileHandle) != -1) {
    if (!List_Insert(threadQueries[curThread],line)) {
      fclose(queryFileHandle);
      return 1;
    }
    curThread = (curThread + 1) % numThreads;
  }
  free(line);
  // Close queries file
  fclose(queryFileHandle);
  // Create the numThreads
  pthread_t threads[numThreads];
  int threadErr;
  pthread_mutex_lock(&threadsMutex);
  for (i = 0;i < numThreads;i++) {
    if ((threadErr = pthread_create(&threads[i],NULL,thread_function,(void*)threadQueries[i]))) {
      thread_error("whoClient thread creation error",threadErr);
      return 1;
    }
  }
  threadsCreated = TRUE;
  pthread_cond_signal(&threadsCreatedCond);
  pthread_mutex_unlock(&threadsMutex);
  // Wait for the threads to finish
  int threadStatus;
  for (i = 0;i < numThreads;i++) {
    if ((threadErr = pthread_join(threads[i],(void**)&threadStatus))) {
      thread_error("whoClient thread join error",threadErr);
    }
  }
  // Destroy the lists
  for (i = 0;i < numThreads;i++) {
    List_Destroy(&threadQueries[i]);
  }
  return 0;
}
