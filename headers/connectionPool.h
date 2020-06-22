#ifndef CONNECTIONPOOL_H
#define CONNECTIONPOOL_H

#include <pthread.h>
#include <netinet/in.h>

// Definitions for connection pool with circular buffer(Similar to producer-consumer).
// Here we have 1 producer(the main thread) and numThreads consumers
enum ConnectionType {STATISTICS = 0,QUERIES = 1, WHOSERVER = 2};

typedef struct {
  int socketDescriptor;
  enum ConnectionType type;
  struct in_addr ip;
  in_port_t port;
} Connection;

typedef struct {
	Connection *connections;
  pthread_t *threads;
	unsigned int size;
  unsigned int numThreads;
	unsigned int start;
	unsigned int end;
	unsigned int count;
} ConnectionPool;

void ConnectionPool_Initialize(ConnectionPool*,unsigned int,unsigned int,void *(*connection_handler)(void*),void*);
void ConnectionPool_AddConnection(ConnectionPool*,Connection);
Connection ConnectionPool_GetConnection(ConnectionPool*);
void ConnectionPool_Destroy(ConnectionPool*);

#endif