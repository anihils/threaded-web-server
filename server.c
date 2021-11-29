#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/syscall.h>    
#include <sys/mman.h>
#include <sys/stat.h>  // For mode constants
#include <fcntl.h>     // For O_* constants
#include <pthread.h>
#include "helper.h"
#include "request.h"

typedef struct buffer_t {
  int *requests;  // pointer to array of requests
  int size; // number of requests in buffer
} buffer_t;

typedef struct threadpool_t {
  pthread_t *threads;       // Array of worker threads
  int thread_count;         // Number of threads
} threadpool_t;

static threadpool_t *pool;
static buffer_t *buffer;
static int PAGESIZE;
pthread_cond_t full = PTHREAD_COND_INITIALIZER;  // wait if buffer is full
pthread_cond_t empty = PTHREAD_COND_INITIALIZER;  // wait if buffer is empty
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;  // buffer lock
char shm_name[25];  // Confirm size does not cause buffer overflow
void *shm_ptr;
void *task(void *arg);

void getargs(int *port, int *threads, int *buf_size, char *shm_name, int argc, char *argv[])
{
  if (argc != 5) {
    fprintf(stderr, "Usage: server [port_num] [threads] [buffers] [shm_name]\n");
    exit(1);
  }

  *port = atoi(argv[1]);
  *threads = atoi(argv[2]);
  *buf_size = atoi(argv[3]);
  if(*port == 22 || *port < 0 || *threads < 0 || *buf_size < 0) {
    exit(1);
  }

  shm_name = strcpy(shm_name, argv[4]);  //FIXME: Check for buffer overflow
}

void init_globals(int threads, int buf_size) {
  PAGESIZE = getpagesize();
  
  // Allocate memory for buffer
  buffer = malloc(sizeof(buffer_t));
  buffer->requests = malloc(sizeof(int) * buf_size);
  buffer->size = 0;

  // Allocate heap memory for threadpool struct and threads
  pool = malloc(sizeof(threadpool_t)); 
  pool->threads = malloc(sizeof(pthread_t) * threads);
  
  // Initialising shared memory segment
  int shm_fd, alloc;
  if ((shm_fd = shm_open(shm_name, O_RDWR | O_CREAT, 0660)) < 0) {
    exit(1);
  } else if ((alloc = ftruncate(shm_fd, PAGESIZE)) < 0) {
    exit(1);
  } else if ((shm_ptr = mmap(NULL, PAGESIZE, PROT_READ | PROT_WRITE,
      MAP_SHARED, shm_fd, 0)) == MAP_FAILED) {
    exit(1);
  }

  slot_t *slot_ptr = (slot_t*) shm_ptr;
  memset(shm_ptr, 0, PAGESIZE);
  // Start worker threads
  for(int i = 0; i < threads; i++) {
    pthread_create(&(pool->threads[i]), NULL, &task, (void*) &slot_ptr[i]); 
    pool->thread_count++;
  }
}

void sig_handler(int signum) {
  munmap(shm_ptr, PAGESIZE);
  shm_unlink(shm_name);
  exit(0);
}

/*
 * Algorithm for worker threads:
 * Acquire lock, check buffer size and wait on CV empty if empty
 * Handle request and close connection
 * Remove request from buffer
 * release lock and signal producer thread 
*/
void *task(void *arg) {
  slot_t *slot = (slot_t*) arg; 
  slot->tid = pthread_self();
  //slot_ptr->num_requests = 0;
  //slot_ptr->num_static = 0;
  //slot_ptr->num_dynamic = 0;
  while(1) {  
    pthread_mutex_lock(&lock);  // acquire lock
    while(buffer->size == 0) {  // check if buffer is empty
      pthread_cond_wait(&empty, &lock);  // wait for signal
    }
    int index = buffer->size - 1;
    int connfd = buffer->requests[index];
    requestHandle(connfd, slot);  // completing request
    Close(connfd);
    slot->num_requests++;
    //buffer->requests[index] = 0;
    buffer->size--;  // updating buffer
    pthread_mutex_unlock(&lock);  // release lock
    pthread_cond_signal(&full); // signal thread that buffer is not full
  }
}

int main(int argc, char *argv[])
{
  signal(SIGINT, sig_handler);

  int listenfd, connfd, clientlen, port, threads, buf_size; 
  struct sockaddr_in clientaddr;
 
  getargs(&port, &threads, &buf_size, shm_name, argc, argv);

  init_globals(threads, buf_size);  // initiliase thread pool and buffer
  
  listenfd = Open_listenfd(port);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);
    if(connfd > 0) {
      pthread_mutex_lock(&lock);
      while(buffer->size >= buf_size) {
        pthread_cond_wait(&full, &lock);
      }
      buffer->requests[buffer->size] = connfd; // adding request to buffer
      buffer->size++;
      pthread_mutex_unlock(&lock);
      pthread_cond_signal(&empty);
    }
  }
}
