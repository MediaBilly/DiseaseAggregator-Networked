#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "../headers/list.h"
#include "../headers/hashtable.h"
#include "../headers/utils.h"

// A structure that keeps data related to a specific worker process
typedef struct
{
  char fifo[16];
  List countriesList;
} WorkerData;

// Global variables
unsigned int numWorkers,bufferSize;
string serverIp,serverPort,input_dir;
boolean running = TRUE;
pid_t terminatedChild = -1;
List countryList;
// Maps a worker's pid to a WorkerData structure
HashTable workersDataHT;

void usage() {
  fprintf(stderr,"Usage:./master –w numWorkers -b bufferSize -s serverIP -p serverPort -i input_dir\n");
}

void middle_free() {
  HashTable_Destroy(&workersDataHT,NULL);
  List_Destroy(&countryList);
}

void destroyWorkersData(string pidStr,void *wd,int argc,va_list valist) {
  WorkerData* workerData = (WorkerData*)wd;
  List_Destroy(&(workerData->countriesList));
  unlink(workerData->fifo);
  free(workerData);
  pid_t pid = atoi(pidStr);
  kill(pid,SIGINT);
}

void child_terminated(int signum) {
  // Get the pid of the exited process
  terminatedChild = wait(NULL);
}

void respawn_worker() {
  // Convert the terminated child's pid to string for later use
  char pidStr[sizeof(pid_t) + 1];
  sprintf(pidStr,"%d",terminatedChild);
  // Get the data of the exited worker
  WorkerData* workerData = (WorkerData*)HashTable_SearchKey(workersDataHT,pidStr);
  // Delete old pid entry from worker's data hashtable
  HashTable_Delete(workersDataHT,pidStr,NULL);
  // Fork new worker process
  pid_t newPID = fork();
  // Fork error
  if (newPID == -1) {
    perror("Fork Failed");
  }
  // Child
  else if (newPID == 0) {
    char bufSize[10];
    sprintf(bufSize,"%d",bufferSize);
    execl("./worker","worker",workerData->fifo,input_dir,bufSize,NULL);
    perror("Exec failed");
    exit(1);
  }
  // Parent
  else {
    // Create an entry for the new pid's country list and insert the old list as value
    char newPidStr[sizeof(pid_t) + 1];
    sprintf(newPidStr,"%d",newPID);
    if (!HashTable_Insert(workersDataHT,newPidStr,workerData)) {
      kill(newPID,SIGKILL);
    }
    int fd = open(workerData->fifo,O_WRONLY);
    // Send server ip and port to the worker process
    send_data(fd,serverIp,strlen(serverIp),bufferSize);
    send_data(fd,"\n",strlen("\n"),bufferSize);
    send_data(fd,serverPort,strlen(serverPort),bufferSize);
    send_data(fd,"\n",strlen("\n"),bufferSize);
    // Send the old worker's countries to the new one
    ListIterator it = List_CreateIterator(workerData->countriesList);
    while (it != NULL) {
      char country[strlen(ListIterator_GetValue(it)) + 1];
      memcpy(country,ListIterator_GetValue(it),strlen(ListIterator_GetValue(it)) + 1);
      country[strlen(ListIterator_GetValue(it))] = '\n';
      send_data(fd,country,sizeof(country),bufferSize);
      ListIterator_MoveToNext(&it);
    }
    close(fd);
  }
  terminatedChild = -1;
}

void stop_execution(int signum) {
  running = FALSE;
}

int main(int argc, char const *argv[]) {
  // Register signal handlers that handle execution termination
  signal(SIGINT,stop_execution);
  signal(SIGQUIT,stop_execution);
  // Register child(worker) termination handler
  signal(SIGCHLD,child_terminated);
  // Usage check
  if (argc != 11) {
    usage();
    return 1;
  }
  // Read numWorkers
  if (!strcmp(argv[1],"-w")) {
    numWorkers = atoi(argv[2]);
  } else {
    usage();
    return 1;
  }
  // Read bufferSize
  if (!strcmp(argv[3],"-b")) {
    bufferSize = atoi(argv[4]);
  } else {
    usage();
    return 1;
  }
  // Read serverIp
  if (!strcmp(argv[5],"-s")) {
    serverIp = (string)argv[6];
  } else {
    usage();
    return 1;
  }
  // Read server port
  if (!strcmp(argv[7],"-p")) {
    int portNum = atoi(argv[8]);
    if (0 <= portNum && portNum <= MAX_PORT_NUMBER) {
      serverPort = (string)argv[8];
    } else {
      fprintf(stderr,"Invalid serverPort %s\n",argv[8]);
      return 1;
    }
  } else {
    usage();
    return 1;
  }
  // Read input_dir
  if (!strcmp(argv[9],"-i")) {
    input_dir = (string)argv[10];
  } else {
    usage();
    return 1;
  }
  // Check if numWorkers is > 0
  if (numWorkers == 0) {
    fprintf(stderr,"numWorkers must be > 0\n");
    return 1;
  }
  // Check if bufferSize is > 0
  if (bufferSize == 0) {
    fprintf(stderr,"bufferSize must be > 0\n");
    return 1;
  }
  // Check if input_dir is really a directory
  struct stat input_dir_info;
  if (stat(input_dir,&input_dir_info) != -1) {
    if ((input_dir_info.st_mode & S_IFMT) != S_IFDIR) {
      fprintf(stderr,"%s is not a directory\n",input_dir);
      return 1;
    }
  } else {
    perror("Failed to get input_dir info");
    return 1;
  }
  // Check if 0 < buffer size is <= pipe size
  if (0 > bufferSize || bufferSize > pipe_size()) {
    printf("Invalid buffer size.\n");
    return 1;
  }
  // Open input_dir
  DIR *input_dir_ptr;
  struct dirent *direntp;
  unsigned int totalCountries = 0;
  // Create countries list
  if ((!List_Initialize(&countryList))) {
    return 1;
  }
  // Create worker's data hashtable
  if (!HashTable_Create(&workersDataHT,4*numWorkers)) {
    List_Destroy(&countryList);
    return 1;
  }
  if ((input_dir_ptr = opendir(input_dir)) != NULL) {
    // Scan and count all country directories in input_dir
    while ((direntp = readdir(input_dir_ptr)) != NULL) {
      if (strcmp(direntp->d_name,".") && strcmp(direntp->d_name,"..") && direntp->d_type == DT_DIR) {
        // Add country to the list
        if (!List_Insert(countryList,direntp->d_name)) {
          List_Destroy(&countryList);
          free(input_dir);
          return 1;
        }
        totalCountries++;
      }
    }
    closedir(input_dir_ptr);
  } else {
    fprintf(stderr,"cannot open %s\n",input_dir);
    return 1;
  }
  unsigned int i,j,workerCountries,currentCountries = totalCountries;
  pid_t pid;
  char fifo[numWorkers];
  // Create an iterator for the countries list
  ListIterator countriesIt = List_CreateIterator(countryList);
  // Create worker processes and distribute country directories to them
  for (i = 0;i < numWorkers;i++) {
    // Create named pipes for the current worker process
    sprintf(fifo,"worker%d",i);
    if (mkfifo(fifo,FIFO_PERMS) < 0) {
      perror("Fifo creation error");
      middle_free();
      return 1;
    }
    if ((pid = fork()) == -1) {
      perror("Fork failed");
      middle_free();
      return 1;
    }
    // Child
    else if (pid == 0) {
      char bufSize[10];
      sprintf(bufSize,"%d",bufferSize);
      execl("./worker","worker",fifo,input_dir,bufSize,NULL);
      perror("Exec failed");
      return 1;
    }
    // Parent
    else {
      // Calculate # of countries for the current worker using uniforn distribution round robbin algorithm
      workerCountries = CEIL(currentCountries,numWorkers - i);
      // Open pipe for writing to the worker process
      int fd = open(fifo,O_WRONLY);
      // Create a list of the countries that this process will work on
      List pidCountries;
      if (!List_Initialize(&pidCountries)) {
        middle_free();
        close(fd);
        return 1;
      }
      // Send server ip and port to the worker process
      send_data(fd,serverIp,strlen(serverIp),bufferSize);
      send_data(fd,"\n",strlen("\n"),bufferSize);
      send_data(fd,serverPort,strlen(serverPort),bufferSize);
      send_data(fd,"\n",strlen("\n"),bufferSize);
      // Distribute country directories to the worker process
      for (j = 0;j < workerCountries && countriesIt != NULL;j++) {
        // Insert country to the current worker's list
        if (!List_Insert(pidCountries,ListIterator_GetValue(countriesIt))) {
          middle_free();
          close(fd);
          return 1;
        }
        // Send country to worker process
        char country[strlen(ListIterator_GetValue(countriesIt)) + 1];
        memcpy(country,ListIterator_GetValue(countriesIt),strlen(ListIterator_GetValue(countriesIt)) + 1);
        country[strlen(ListIterator_GetValue(countriesIt))] = '\n';
        send_data(fd,country,sizeof(country),bufferSize);
        ListIterator_MoveToNext(&countriesIt);
      }
      // Initialize worker's data structure
      WorkerData* workerData;
      if ((workerData = (WorkerData*)malloc(sizeof(WorkerData))) != NULL) {
        workerData->countriesList = pidCountries;
        strcpy(workerData->fifo,fifo);
        char pidStr[sizeof(pid_t) + 1];
        sprintf(pidStr,"%d",pid);
        // Insert it to the worker's hash table
        if (!HashTable_Insert(workersDataHT,pidStr,workerData)) {
          List_Destroy(&pidCountries);
          middle_free();
          close(fd);
          return 1;
        }
      } else {
        not_enough_memory();
        List_Destroy(&pidCountries);
        middle_free();
        close(fd);
        return 1;
      }
      currentCountries -= workerCountries;
      // Close the pipe
      close(fd);
    }
  }
  // Wait until a signal is received 
  while (running) {
    if (terminatedChild != -1) {
      respawn_worker();
    }
    pause();
  }
  // Unregister SIGCHLD handler because when the master finishes execution we do not need to respawn children
  signal(SIGCHLD,SIG_DFL);
  // Kill all worker processes
  HashTable_ExecuteFunctionForAllKeys(workersDataHT,destroyWorkersData,0);
  // Wait for workers to finish execution
  for (i = 0;i < numWorkers;i++) {
    int exit_status;
    pid_t exited_pid;
    if ((exited_pid = wait(&exit_status)) == -1) {
      perror("Wait failed");
    }
  }
  // Free all data structures memory
  HashTable_Destroy(&workersDataHT,NULL);
  List_Destroy(&countryList);
  return 0;
}
