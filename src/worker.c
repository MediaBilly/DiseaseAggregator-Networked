#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include "../headers/list.h"
#include "../headers/avltree.h"
#include "../headers/hashtable.h"
#include "../headers/patientRecord.h"
#include "../headers/treestats.h"
#include "../headers/utils.h"

// Declare global variables
List countriesList;
string receiver_fifo,input_dir,serverIp,serverPort;
unsigned int bufferSize;
// Maps each country to another hash table that maps country's diseases to two avl trees, one that contains their records sorted by entry date and one that contains statistics sorted by date 
HashTable recordsHT;
// Maps a record's ID to the record itself (used for /searchPatientRecord recordID command)
HashTable recordsByIdHT;
// A hash table that is used as a set of all the files currently handling
HashTable countryFilesHT;
// Runtime flags
boolean running = TRUE;
boolean sendStats;

void usage() {
  fprintf(stderr,"Usage:./worker receiver_fifo input_dir bufferSize [-nostats]\n");
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
  string statistics;
  if (sendStats) {
    statistics = (string)(malloc(1));
    if (statistics == NULL) {
      return;
    }
    strcpy(statistics,"");
  }
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
          if (sendStats && !HashTable_Create(&filestatsHT,200)) {
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
                        if (sendStats) {
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
          if (sendStats) {
            char header[strlen(date) + strlen(country) + 2];
            sprintf(header,"%s\n%s\n",date,country);
            // Append the header
            stringAppend(&statistics,header);
            HashTable_ExecuteFunctionForAllKeys(filestatsHT,summaryStatistics,1,&statistics);
            // Destroy filestats hash table
            HashTable_ExecuteFunctionForAllKeys(filestatsHT,destroyDiseaseFilestatsFromHT,0);
            HashTable_Destroy(&filestatsHT,NULL);
          }
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
  if (sendStats) {
    // Open fifo_worker_to_aggregator to send summary statistics back to the aggregator
    //int fifo_worker_to_aggregator_fd = open(fifo_worker_to_aggregator,O_WRONLY);
    // Send the statistics to the aggregator
   // send_data(fifo_worker_to_aggregator_fd,statistics,strlen(statistics),bufferSize);
   printf("%s",statistics);
    // Close fifo_worker_to_aggregator_fd
    //close(fifo_worker_to_aggregator_fd);
    free(statistics);
  }
  // Never send statistics back ?
  sendStats = FALSE;
}

void destroyCountryHTdiseaseTables(string disease,void *ht,int argc,va_list valist) {
  HashTable_Destroy((HashTable*)&ht,(int (*)(void**))AvlTree_Destroy);
}

void stop_execution(int signum) {
  running = FALSE;
}

int main(int argc, char const *argv[]) {
  // Register signal handlers that handle execution termination
  signal(SIGINT,stop_execution);
  signal(SIGQUIT,stop_execution);
  // Usage check
  if (argc < 4 || argc > 5) {
    usage();
    return 1;
  }
  // Read receiver_fifo(pipe that receives data from master process)
  receiver_fifo = (string)argv[1];
  // Read input_dir
  input_dir = (string)argv[2];
  // Read bufferSize;
  bufferSize = atoi(argv[3]);
  // No stats is no necessary argument. If specified, the worker will not send any stats to the whoServer.
  // It is set when this worker is a replacement of another one which was terminated
  if (argc == 5) {
    if (strcmp(argv[4],"-nostats")) {
      usage();
      return 1;
    } else {
      sendStats = FALSE;
    }
  } else {
    sendStats = TRUE;
  }
  // Open receiver fifo to read data from master
  int receiverFd = open(receiver_fifo,O_RDONLY);
  string masterData = receive_data(receiverFd,bufferSize,TRUE);
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
  printf("LISTENING on %s:%s\n",serverIp,serverPort);
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
  while(running);

  // Destroy diseas hash tables in country hash table
  HashTable_ExecuteFunctionForAllKeys(recordsHT,destroyCountryHTdiseaseTables,0);
  // Destroy country hash table
  HashTable_Destroy(&recordsHT,NULL);
  // Destroy records only from id mapped hash table
  HashTable_Destroy(&recordsByIdHT,(int (*)(void**))PatientRecord_Destroy);
  // Destroy country files hash table
  HashTable_Destroy(&countryFilesHT,(int (*)(void**))DestroyString);
  List_Destroy(&countriesList);
  return 0;
}
