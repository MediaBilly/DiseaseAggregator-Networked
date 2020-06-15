#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "../headers/list.h"
#include "../headers/avltree.h"
#include "../headers/hashtable.h"
#include "../headers/patientRecord.h"
#include "../headers/treestats.h"
#include "../headers/utils.h"

// Declare global variables
List countriesList;
string receiver_fifo,input_dir,whoServerIp,whoServerPort;
unsigned int bufferSize;
// Maps each country to another hash table that maps country's diseases to two avl trees, one that contains their records sorted by entry date and one that contains statistics sorted by date 
HashTable recordsHT;
// Maps a record's ID to the record itself (used for /searchPatientRecord recordID command)
HashTable recordsByIdHT;
// A hash table that is used as a set of all the files currently handling
HashTable countryFilesHT;
// Runtime flags
boolean running = TRUE;
struct sockaddr_in whoServerAddress;
// Address and port where the worker listens for queries
struct sockaddr_in myAddress;
in_port_t myPort;
struct hostent *whoServerEntry;

void usage() {
  fprintf(stderr,"Usage:./worker receiver_fifo input_dir bufferSize\n");
}

void destroyDiseaseFilestatsFromHT(string filename,void *statsstruct,int argc,va_list valist) {
  free(statsstruct);
}

void summaryStatistics(string disease,void *statsstruct,int argc,va_list valist) {
  filestat *stats = (filestat*)statsstruct;
  string* statsStr = va_arg(valist,string*);
  char buf[strlen(disease) + 119 + digits(stats->years0to20) + digits(stats->years21to40) + digits(stats->years41to60) + digits(stats->years60plus)];
  memset(buf,0,sizeof(buf));
  sprintf(buf,"%s\nAge range 0-20 years: %d cases\nAge range 21-40 years: %d cases\nAge range 41-60 years: %d cases\nAge range 60+ years: %d cases\n\n",disease,stats->years0to20,stats->years21to40,stats->years41to60,stats->years60plus);
  stringAppend(statsStr,buf);
}

void read_input_files() {
  List exitRecords;
  if (!List_Initialize(&exitRecords)) {
    return;
  }
  // Foreach country read all the files contained in it's folder and save the records
  ListIterator countriesIterator = List_CreateIterator(countriesList);
  string statistics = (string)(malloc(1));
  if (statistics == NULL) {
    return;
  }
  strcpy(statistics,"");
  while (countriesIterator != NULL) {
    string country = ListIterator_GetValue(countriesIterator);
    DIR *dir_ptr;
    struct dirent *direntp;
    char path[strlen(input_dir) + strlen(country) + 2];
    sprintf(path,"%s/%s",input_dir,country);
    // Open country directory
    if ((dir_ptr = opendir(path)) != NULL) {
      // Read all the files in it
      while ((direntp = readdir(dir_ptr)) != NULL) {
        if (direntp->d_type == DT_REG) {
          string date = direntp->d_name;
          // Open the curent file
          FILE *recordsFile;
          char filePath[strlen(input_dir) + strlen(country) + strlen(date) + 3];
          sprintf(filePath,"%s/%s",path,date);
          // Check if that file was already read
          if (HashTable_SearchKey(countryFilesHT,filePath) != NULL) {
            continue;
          }
          if ((recordsFile = fopen(filePath,"r")) == NULL) {
            perror("fopen");
            continue;
          }
          // Maps a disease to a filestat structure to hold summary statistics for that file
          HashTable filestatsHT;
          if (!HashTable_Create(&filestatsHT,200)) {
            continue;
          }
          // Insert the file path in the country files hashtable with an empty list
          string pathCopy;
          if ((pathCopy = CopyString(filePath)) == NULL) {
            continue;
          }
          if (!HashTable_Insert(countryFilesHT,path,pathCopy)) {
            continue;
          }
          // Read all the lines(records) from the file
          string line = NULL;
          size_t len = 0;
          while (getline(&line,&len,recordsFile) != -1) {
            string recordId = strtok(line," ");
            string type = strtok(NULL," ");
            string patientFirstName = strtok(NULL," ");
            string patientLastName = strtok(NULL," ");
            string disease = strtok(NULL," ");
            int age = atoi(strtok(NULL," "));
            if (!strcmp(type,"ENTER")) {
              // Patient Enters
              // Check if he already entered
              if (HashTable_SearchKey(recordsByIdHT,recordId) == NULL) {
                // Create the record object
                patientRecord record;
                // Insert the record to the id mapped hash table
                if ((record = PatientRecord_Create(recordId,patientFirstName,patientLastName,disease,date,age)) != NULL) {
                  if (HashTable_Insert(recordsByIdHT,recordId,record)) {
                    // Insert the record to the country and disease mapped table
                    // Check if record's disease already exists in that country hash table
                    HashTable diseaseHT;
                    if ((diseaseHT = HashTable_SearchKey(recordsHT,country)) == NULL) {
                      // Not exisits so create a hashtable for that disease
                      if (HashTable_Create(&diseaseHT,200)) {
                        HashTable_Insert(recordsHT,country,diseaseHT);
                      }
                    }
                    if (diseaseHT != NULL) {
                      // Chech if disease hash table already contains an avl tree for that disease
                      AvlTree diseaseTree;
                      if ((diseaseTree = HashTable_SearchKey(diseaseHT,disease)) == NULL) {
                        // If not create one
                        if (AvlTree_Create(&diseaseTree)) {
                          // And then insert it to the disease hash table
                          HashTable_Insert(diseaseHT,disease,diseaseTree);
                        }
                      }
                      // Insert the record to the tree
                      if (diseaseTree != NULL) {
                        AvlTree_Insert(diseaseTree,record);
                        // Update statistics for that file
                        // Update file statistics 
                        filestat *stats;
                        if ((stats = (filestat*)HashTable_SearchKey(filestatsHT,disease)) == NULL) {
                          if ((stats = (filestat*)malloc(sizeof(filestat))) != NULL) {
                            memset(stats,0,sizeof(filestat));
                            if (!HashTable_Insert(filestatsHT,disease,stats)) {
                              free(stats);
                              continue;
                            }
                          }
                        }
                        if (stats != NULL) {
                          if (0 <= age && age <= 20) {
                            stats->years0to20++;
                          } else if (21 <= age && age <= 40) {
                            stats->years21to40++;
                          } else if (41 <= age && age <= 60) {
                            stats->years41to60++;
                          } else {
                            stats->years60plus++;
                          }
                        }
                      }
                    }
                  }
                }
              } else {
                fprintf(stderr,"ERROR: Patient record %s %s %s %s %s %d tried to enter the hospital twice.\n",recordId,type,patientFirstName,patientLastName,disease,age);
              }
            } else if (!strcmp(type,"EXIT")) {
              // Patient exits
              // Save the id and the date of the exited record in a list
              char exitRecordData[strlen(recordId) + strlen(date) + 1];
              sprintf(exitRecordData,"%s %s",recordId,date);
              List_Insert(exitRecords,exitRecordData);
            }
          }
          // Generate file's summary statistics to the aggregator
          char header[strlen(date) + strlen(country) + 2];
          sprintf(header,"%s\n%s\n",date,country);
          // Append the header
          stringAppend(&statistics,header);
          HashTable_ExecuteFunctionForAllKeys(filestatsHT,summaryStatistics,1,&statistics);
          // Destroy filestats hash table
          HashTable_ExecuteFunctionForAllKeys(filestatsHT,destroyDiseaseFilestatsFromHT,0);
          HashTable_Destroy(&filestatsHT,NULL);
          // Get stats tree
          free(line);
          fclose(recordsFile);
        }
      }
      // Close countries directory
      closedir(dir_ptr);
    } else {
      fprintf(stderr,"Process %d could not open directory %s\n",getpid(),path);
    }
    ListIterator_MoveToNext(&countriesIterator);
  }
  // Insert EXIT records
  ListIterator exitRecordsIt = List_CreateIterator(exitRecords);
  while (exitRecordsIt != NULL) {
    string exitData = ListIterator_GetValue(exitRecordsIt);
    string recordId = strtok(exitData," ");
    string exitDate = strtok(NULL," ");
    patientRecord record;
    // Check if he already entered
    if ((record = HashTable_SearchKey(recordsByIdHT,recordId)) != NULL) {
      // If so mark him as exited
      PatientRecord_Exit(record,exitDate);
    } else {
      fprintf(stderr,"ERROR: Patient record with id %s tried to exit the hospital at %s before entering.\n",recordId,exitDate);
    }
    ListIterator_MoveToNext(&exitRecordsIt);
  }
  List_Destroy(&exitRecords);
  // Find whoServer's address
  if ((whoServerEntry = gethostbyname(whoServerIp)) == NULL) {
    herror("Worker failed to resolve whoServer's hostname");
    exit(EXIT_FAILURE);
  }
  whoServerAddress.sin_family = AF_INET;
  memcpy(&whoServerAddress.sin_addr,whoServerEntry->h_addr,whoServerEntry->h_length);
  whoServerAddress.sin_port = htons(atoi(whoServerPort));
  // Connect to whoServer
  int whoServerSocket;
  if ((whoServerSocket = socket(AF_INET,SOCK_STREAM,0)) < 0) {
    perror("Worker could not connect to whoServer");
    HashTable_Destroy(&recordsByIdHT,NULL);
    HashTable_Destroy(&recordsHT,NULL);
    List_Destroy(&countriesList);
    exit(EXIT_FAILURE);
  }
  if (connect(whoServerSocket,(struct sockaddr*)&whoServerAddress,sizeof(whoServerAddress)) < 0) {
    perror("Worker thread could not connect to whoServer");
    close(whoServerSocket);
    HashTable_Destroy(&recordsByIdHT,NULL);
    HashTable_Destroy(&recordsHT,NULL);
    List_Destroy(&countriesList);
    exit(EXIT_FAILURE);
  }
  // Send him the port number
  send_data_to_socket(whoServerSocket,(char*)&myPort,sizeof(myAddress.sin_port),BUFFER_SIZE);
  // Send him the statistics
  send_data_to_socket(whoServerSocket,statistics,strlen(statistics),BUFFER_SIZE);
  free(statistics);
  close(whoServerSocket);
}

void destroyCountryHTdiseaseTables(string disease,void *ht,int argc,va_list valist) {
  HashTable_Destroy((HashTable*)&ht,(int (*)(void**))AvlTree_Destroy);
}

void stop_execution(int signum) {
  running = FALSE;
}

void diseaseFrequencyAllCountries(string country,void *ht,int argc,va_list valist) {
  string virusName = va_arg(valist,string);
  time_t date1 = va_arg(valist,time_t);
  time_t date2 = va_arg(valist,time_t);
  unsigned int *result = va_arg(valist,unsigned int*);
  // Get disease's avl tree
  AvlTree tree = HashTable_SearchKey((HashTable)ht,virusName);
  // Check if it really exists
  if (tree != NULL) {
    // Update the result with the current country results
    *result += AvlTree_NumRecordsInDateRange(tree,date1,date2,FALSE);
  }
}

void numPatientAdmissionsAllCountries(string country,void *ht,int argc,va_list valist) {
  string virusName = va_arg(valist,string);
  time_t date1 = va_arg(valist,time_t);
  time_t date2 = va_arg(valist,time_t);
  string *answer = va_arg(valist,string*);
  // Get disease's avl tree
  AvlTree tree = HashTable_SearchKey((HashTable)ht,virusName);
  // Check if it really exists
  if (tree != NULL) {
    // Send current country results to the worker
    unsigned int result = AvlTree_NumRecordsInDateRange(tree,date1,date2,FALSE);
    char dataToSend[strlen(country) + digits(result) + 2];
    sprintf(dataToSend,"%s %u\n",country,result);
    stringAppend(answer,dataToSend);
  } else {
    char dataToSend[strlen(country) + 3];
    sprintf(dataToSend,"%s 0\n",country);
    stringAppend(answer,dataToSend);
  }
}

void numPatientDischargesAllCountries(string country,void *ht,int argc,va_list valist) {
  string virusName = va_arg(valist,string);
  time_t date1 = va_arg(valist,time_t);
  time_t date2 = va_arg(valist,time_t);
  string *answer = va_arg(valist,string*);
  // Get disease's avl tree
  AvlTree tree = HashTable_SearchKey((HashTable)ht,virusName);
  // Check if it really exists
  if (tree != NULL) {
    // Send current country results to the worker
    unsigned int result = AvlTree_NumRecordsInDateRange(tree,date1,date2,TRUE);
    char dataToSend[strlen(country) + digits(result) + 2];
    sprintf(dataToSend,"%s %u\n",country,result);
    stringAppend(answer,dataToSend);
  } else {
    char dataToSend[strlen(country) + 3];
    sprintf(dataToSend,"%s 0\n",country);
    stringAppend(answer,dataToSend);
  }
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
  // Read receiver_fifo(pipe that receives data from master process)
  receiver_fifo = (string)argv[1];
  // Read input_dir
  input_dir = (string)argv[2];
  // Read bufferSize;
  bufferSize = atoi(argv[3]);
  // Open receiver fifo to read data from master
  int receiverFd = open(receiver_fifo,O_RDONLY);
  string masterData = receive_data(receiverFd,bufferSize,TRUE);
  close(receiverFd);
  if (masterData == NULL) {
    return 1;
  }
  // Start listening for queries
  int queriesSocket;
  if ((queriesSocket = socket(AF_INET,SOCK_STREAM | SOCK_NONBLOCK,0)) == -1) {
    perror("Worker failed to create queries socket");
    return 1;
  }
  myAddress.sin_family = AF_INET;
  myAddress.sin_addr.s_addr = htonl(INADDR_ANY);
  myAddress.sin_port = 0;// Bind to the first available port
  if (bind(queriesSocket,(struct sockaddr*)&myAddress,sizeof(myAddress)) < 0) {
    perror("Worker failed to bind queries socket");
    return 1;
  }
  // Start listening for whoClients incoming connections
  if (listen(queriesSocket,100) < 0) {
    perror("Queries socket failed to start listening to incoming whoClients");
    return 1;
  }
  struct sockaddr_in listenAddr;
  memset(&listenAddr,0,sizeof(listenAddr));
  socklen_t len = sizeof(listenAddr);
  if (getsockname(queriesSocket,(struct sockaddr*)&listenAddr,&len) == -1) {
    perror("getsockname");
    close(queriesSocket);
    return 1;
  }
  myPort = ntohs(listenAddr.sin_port);
  printf("Listening for queries on %s:%d\n",inet_ntoa(listenAddr.sin_addr),myPort);
  // Read server ip
  whoServerIp = CopyString(strtok(masterData,"\n"));
  // Read server port
  whoServerPort = CopyString(strtok(NULL,"\n"));
  // Initialize countries list
  if (!List_Initialize(&countriesList)) {
    return 1;
  }
  // Initialize hashtables
  if (!HashTable_Create(&recordsHT,200)) {
    List_Destroy(&countriesList);
    return 1;
  }
  if (!HashTable_Create(&recordsByIdHT,200)) {
    HashTable_Destroy(&recordsHT,NULL);
    List_Destroy(&countriesList);
    return 1;
  }
  if (!HashTable_Create(&countryFilesHT,200)) {
    HashTable_Destroy(&recordsByIdHT,NULL);
    HashTable_Destroy(&recordsHT,NULL);
    List_Destroy(&countriesList);
  }
  // Read the countries sent by diseaseAggregator;
  string country = strtok(NULL,"\n");
  while (country != NULL) {
    List_Insert(countriesList,country);
    country = strtok(NULL,"\n");
  }
  // Free memory needed for receiving the countries
  free(masterData);
  // Read input files
  read_input_files();
  // Accept commands
  fd_set fdset;
  int connectedWhoServer;
  struct sockaddr_in connectedWhoServerAddress;
  socklen_t connectedWhoServerAddressLength = 0;
  unsigned int cmdArgc;
  string *args,command;
  while(running) {
    FD_ZERO(&fdset);
    FD_SET(queriesSocket,&fdset);
    if (select(queriesSocket + 1,&fdset,NULL,NULL,NULL) != -1) {
      if (FD_ISSET(queriesSocket,&fdset)) {
        if ((connectedWhoServer = accept(queriesSocket,(struct sockaddr*)&connectedWhoServerAddress,&connectedWhoServerAddressLength)) < 0) {
          perror("Worker failed to accept query incoming connection");
          return 1;
        }
        // Get accepted connection address
        struct sockaddr_in acceptedAddress;
        socklen_t len = 0;
        if (getsockname(connectedWhoServer,(struct sockaddr*)&acceptedAddress,&len) != -1) {
          printf("New connection accepted from %s:%d\n",inet_ntoa(acceptedAddress.sin_addr),(int)ntohs(acceptedAddress.sin_port));
        } else {
          perror("getsockname");
          close(connectedWhoServer);
        }
        char *query = receive_data_from_socket(connectedWhoServer,BUFFER_SIZE,TRUE);
        printf("Received query:%s",query);
        if (query != NULL) {
          // Read command(query)
          cmdArgc = wordCount(query);
          args = SplitString(query," ");
          command = args[0];
          // Execute command
          if (!strcmp(command,"/diseaseFrequency")) {
            // Get virusName(disease)
            string virusName = args[1];
            // Parse dates
            struct tm tmpTime;
            memset(&tmpTime,0,sizeof(struct tm));
            if (strptime(args[2],"%d-%m-%Y",&tmpTime) != NULL) {
              time_t date1 = mktime(&tmpTime);
              if (strptime(args[3],"%d-%m-%Y",&tmpTime) != NULL) {
                time_t date2 = mktime(&tmpTime);
                // Date  Parsing done
                // Check if country was specified
                if (cmdArgc == 5) {
                  // Country specified
                  string country = args[4];
                  // Get country's disease hash table
                  HashTable virusHT = HashTable_SearchKey(recordsHT,country);
                  // Check if specified country exists
                  if (virusHT != NULL) {
                    // Get disease's avl tree(if specified virus does not exist returns NULL)
                    AvlTree tree = HashTable_SearchKey(virusHT,virusName);
                    // Check if specified virus exists
                    if (tree != NULL) {
                      char result[sizeof(unsigned int) + 1];
                      sprintf(result,"%u",AvlTree_NumRecordsInDateRange(tree,date1,date2,FALSE));
                      send_data_to_socket(connectedWhoServer,result,strlen(result),BUFFER_SIZE);
                    } else {
                      send_data_to_socket(connectedWhoServer,"0",strlen("0"),BUFFER_SIZE);
                    }
                  } else {
                    send_data_to_socket(connectedWhoServer,"0",strlen("0"),BUFFER_SIZE);
                  }
                } else if (cmdArgc == 4) {
                  unsigned int result = 0;
                  HashTable_ExecuteFunctionForAllKeys(recordsHT,diseaseFrequencyAllCountries,4,virusName,date1,date2,&result);
                  char resultStr[sizeof(unsigned int)];
                  sprintf(resultStr,"%u",result);
                  send_data_to_socket(connectedWhoServer,resultStr,strlen(resultStr),BUFFER_SIZE);
                } else {
                  send_data_to_socket(connectedWhoServer,"0",strlen("0"),BUFFER_SIZE);
                }
              } else {
                fprintf(stderr,"date2 parsing failed.\n");
                send_data_to_socket(connectedWhoServer,"0",strlen("0"),BUFFER_SIZE);
              }
            } else {
              fprintf(stderr,"date1 parsing failed.\n");
              send_data_to_socket(connectedWhoServer,"0",strlen("0"),BUFFER_SIZE);
            }
          } else if (!strcmp(command,"/topk-AgeRanges")) {
            // Get arguments
            unsigned int k = atoi(args[1]);
            string country = args[2];
            string disease = args[3];
            // Parse dates
            struct tm tmpTime;
            memset(&tmpTime,0,sizeof(struct tm));
            if (strptime(args[4],"%d-%m-%Y",&tmpTime) != NULL) {
              time_t date1 = mktime(&tmpTime);
              if (strptime(args[5],"%d-%m-%Y",&tmpTime) != NULL) {
                time_t date2 = mktime(&tmpTime);
                // Date  Parsing done
                // Get country's disease hash table
                HashTable diseaseHT = HashTable_SearchKey(recordsHT,country);
                // Check if it really exists
                if (diseaseHT != NULL) {
                  // Get disease's avl tree
                  AvlTree tree = HashTable_SearchKey(diseaseHT,disease);
                  // Check if it really exists
                  if (tree != NULL) {
                    // Calculate and send result to the whoServer
                    AvlTree_topk_Age_Ranges(tree,date1,date2,k,connectedWhoServer,BUFFER_SIZE);
                  } else {
                    send_data_to_socket(connectedWhoServer,"nf",strlen("nf"),BUFFER_SIZE);
                  }
                } else {
                  send_data_to_socket(connectedWhoServer,"nf",strlen("nf"),BUFFER_SIZE);
                }
              } else {
                fprintf(stderr,"date2 parsing failed.\n");
                send_data_to_socket(connectedWhoServer,"nf",strlen("nf"),BUFFER_SIZE);
              }
            } else {
              fprintf(stderr,"date1 parsing failed.\n");
              send_data_to_socket(connectedWhoServer,"nf",strlen("nf"),BUFFER_SIZE);
            }
          } else if (!strcmp(command,"/searchPatientRecord")) {
            // Get record id
            string recordId = args[1];
            patientRecord rec;
            if ((rec = HashTable_SearchKey(recordsByIdHT,recordId)) != NULL) {
              // Found
              string recStr = PatientRecord_ToString(rec);
              send_data_to_socket(connectedWhoServer,recStr,strlen(recStr),BUFFER_SIZE);
              free(recStr);
            } else {
              // Not found
              send_data_to_socket(connectedWhoServer,"nf",strlen("nf"),BUFFER_SIZE);
            }
          } else if (!strcmp(command,"/numPatientAdmissions")) {
            // Get virusName(disease)
            string virusName = args[1];
            // Parse dates
            struct tm tmpTime;
            memset(&tmpTime,0,sizeof(struct tm));
            if (strptime(args[2],"%d-%m-%Y",&tmpTime) != NULL) {
              time_t date1 = mktime(&tmpTime);
              if (strptime(args[3],"%d-%m-%Y",&tmpTime) != NULL) {
                time_t date2 = mktime(&tmpTime);
                // Date  Parsing done
                // Check if country was specified
                if (cmdArgc == 5) {
                  // Country specified
                  string country = args[4];
                  // Get country's disease hash table
                  HashTable virusHT = HashTable_SearchKey(recordsHT,country);
                  // Check if it really exists
                  if (virusHT != NULL) {
                    // Get disease's avl tree
                    AvlTree tree = HashTable_SearchKey(virusHT,virusName);
                    // Check if it really exists
                    if (tree != NULL) {
                      char result[strlen(country) + sizeof(unsigned int) + 2];
                      sprintf(result,"%s %u\n",country,AvlTree_NumRecordsInDateRange(tree,date1,date2,FALSE));
                      send_data_to_socket(connectedWhoServer,result,strlen(result),BUFFER_SIZE);
                    } else {
                      char result[strlen(country) + sizeof(unsigned int) + 2];
                      sprintf(result,"nf");
                      send_data_to_socket(connectedWhoServer,result,strlen(result),BUFFER_SIZE);
                    }
                  } else {
                    send_data_to_socket(connectedWhoServer,"nf",strlen("nf"),BUFFER_SIZE);
                  }
                } else if (cmdArgc == 4) {
                  string answer = (string)malloc(1);
                  strcpy(answer,"");
                  HashTable_ExecuteFunctionForAllKeys(recordsHT,numPatientAdmissionsAllCountries,4,virusName,date1,date2,&answer);
                  send_data_to_socket(connectedWhoServer,answer,strlen(answer),BUFFER_SIZE);
                  DestroyString(&answer);
                } else {
                  send_data_to_socket(connectedWhoServer,"nf",strlen("nf"),BUFFER_SIZE);
                }
              } else {
                fprintf(stderr,"date2 parsing failed.\n");
                send_data_to_socket(connectedWhoServer,"nf",strlen("nf"),BUFFER_SIZE);
              }
            } else {
              fprintf(stderr,"date1 parsing failed.\n");
              send_data_to_socket(connectedWhoServer,"nf",strlen("nf"),BUFFER_SIZE);
            }
          } else if (!strcmp(command,"/numPatientDischarges")) {
            // Get virusName(disease)
            string virusName = args[1];
            // Parse dates
            struct tm tmpTime;
            memset(&tmpTime,0,sizeof(struct tm));
            if (strptime(args[2],"%d-%m-%Y",&tmpTime) != NULL) {
              time_t date1 = mktime(&tmpTime);
              if (strptime(args[3],"%d-%m-%Y",&tmpTime) != NULL) {
                time_t date2 = mktime(&tmpTime);
                // Date  Parsing done
                // Check if country was specified
                if (cmdArgc == 5) {
                  // Country specified
                  string country = args[4];
                  // Get country's disease hash table
                  HashTable virusHT = HashTable_SearchKey(recordsHT,country);
                  // Check if it really exists
                  if (virusHT != NULL) {
                    // Get disease's avl tree
                    AvlTree tree = HashTable_SearchKey(virusHT,virusName);
                    // Check if it really exists
                    if (tree != NULL) {
                      char result[strlen(country) + sizeof(unsigned int) + 2];
                      sprintf(result,"%s %u\n",country,AvlTree_NumRecordsInDateRange(tree,date1,date2,TRUE));
                      send_data_to_socket(connectedWhoServer,result,strlen(result),BUFFER_SIZE);
                    } else {
                      char result[strlen(country) + sizeof(unsigned int) + 2];
                      sprintf(result,"nf");
                      send_data_to_socket(connectedWhoServer,result,strlen(result),BUFFER_SIZE);
                    }
                  } else {
                    send_data_to_socket(connectedWhoServer,"nf",strlen("nf"),BUFFER_SIZE);
                  }
                } else if (cmdArgc == 4) {
                  string answer = malloc(1);
                  strcpy(answer,"");
                  HashTable_ExecuteFunctionForAllKeys(recordsHT,numPatientDischargesAllCountries,4,virusName,date1,date2,&answer);
                  send_data_to_socket(connectedWhoServer,answer,strlen(answer),BUFFER_SIZE);
                  free(answer);
                } else {
                  send_data_to_socket(connectedWhoServer,"nf",strlen("nf"),BUFFER_SIZE);
                }
              } else {
                fprintf(stderr,"date2 parsing failed.\n");
                send_data_to_socket(connectedWhoServer,"nf",strlen("nf"),BUFFER_SIZE);
              }
            } else {
              fprintf(stderr,"date1 parsing failed.\n");
              send_data_to_socket(connectedWhoServer,"nf",strlen("nf"),BUFFER_SIZE);
            }
          } else {
            send_data_to_socket(connectedWhoServer,"Wrong command\n",strlen("Wrong command\n"),BUFFER_SIZE);
          }
        }
        free(query);
        free(args);
        close(connectedWhoServer);
        printf("%s:%d disconnected\n",inet_ntoa(acceptedAddress.sin_addr),(int)ntohs(acceptedAddress.sin_port));
      }
    }
  }
  // Close queries socket
  close(queriesSocket);
  // Destroy diseas hash tables in country hash table
  HashTable_ExecuteFunctionForAllKeys(recordsHT,destroyCountryHTdiseaseTables,0);
  // Destroy country hash table
  HashTable_Destroy(&recordsHT,NULL);
  // Destroy records only from id mapped hash table
  HashTable_Destroy(&recordsByIdHT,(int (*)(void**))PatientRecord_Destroy);
  // Destroy country files hash table
  HashTable_Destroy(&countryFilesHT,(int (*)(void**))DestroyString);
  List_Destroy(&countriesList);
  free(whoServerIp);
  free(whoServerPort);
  return 0;
}
