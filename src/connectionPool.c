#include <stdlib.h>
#include <pthread.h>
#include "../headers/connectionPool.h"
#include "../headers/utils.h"

pthread_mutex_t poolMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t pool_nonempty = PTHREAD_COND_INITIALIZER;
pthread_cond_t pool_nonfull = PTHREAD_COND_INITIALIZER;

/* Connection Pool functions */

void ConnectionPool_Initialize(ConnectionPool* pool,unsigned int size,unsigned int numThreads,void *(*connection_handler)(void*),void *arg) {
  // Allocate arrays
  if ((pool->connections = (Connection*)malloc(size*sizeof(Connection))) == NULL) {
    not_enough_memory();
    exit(EXIT_FAILURE);
  }
  if ((pool->threads = (pthread_t*)malloc(numThreads*sizeof(pthread_t))) == NULL) {
    not_enough_memory();
    free(pool->connections);
    exit(EXIT_FAILURE);
  }
  // Initialize parameters
  pool->size = size;
  pool->numThreads = numThreads;
  pool->start = pool->end = pool->count = 0;
  // Spawn the threads
  unsigned int i;
  int threadErr;
  for (i = 0;i < numThreads;i++) {
    if ((threadErr = pthread_create(&(pool->threads[i]),NULL,connection_handler,arg))) {
      thread_error("whoServer thread creation error",threadErr);
      exit(EXIT_FAILURE);
    }
  }
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
  // Terminate the threads
  unsigned int i;
  for (i = 0;i < pool->numThreads;i++) {
    pthread_cancel(pool->threads[i]);
    pthread_mutex_unlock(&poolMutex);
    pthread_join(pool->threads[i],NULL);
  }
  free(pool->threads);
  free(pool->connections);
}