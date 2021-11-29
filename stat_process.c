#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>  // For mode constants
#include <fcntl.h>     // For O_* constants
#include <time.h>
#include "helper.h"
#include "request.h"

void getargs(char *shm_name, int *sleeptime_ms, int *num_threads, int argc, char *argv[])
{
  if (argc != 4) {
    fprintf(stderr, "Usage: stat_process [shm_name] [sleeptime_ms] [num_threads]\n");
    exit(1);
  }
  strcpy(shm_name, argv[1]);
  *sleeptime_ms = atoi(argv[2]);
  if (*sleeptime_ms < 0) {
    exit(1);
  }
  *num_threads = atoi(argv[3]);
  if (*num_threads < 0) {
    exit(1);
  }
}

int main(int argc, char *argv[])
{
  int PAGESIZE = getpagesize();
  void *shm_ptr;
  int sleeptime_ms, num_threads, shm_fd;
  char shm_name[25];

  getargs(shm_name, &sleeptime_ms, &num_threads, argc, argv);

  // Initialising shared memory segment
  if ((shm_fd = shm_open(shm_name, O_RDWR, 0660)) < 0) {
    exit(1);
  } else if ((shm_ptr = mmap(NULL, PAGESIZE, PROT_READ | PROT_WRITE,
      MAP_SHARED, shm_fd, 0)) == MAP_FAILED) {
    exit(1);
  }

  // Creating time_spec struct
  struct timespec timespec;
  struct timespec *time = &timespec;
  time->tv_sec = 0;  // seconds
  time->tv_nsec = (sleeptime_ms)*1000000;  // nanoseconds 

  slot_t *slot_ptr = (slot_t*) shm_ptr; // Casting page into memory
  int i = 1;
  while(1) {
    nanosleep(time, NULL); 
    // printing stats
    printf("\n%d\n", i);
    for(int j = 0; j < num_threads; j++) {
      slot_t *slot = &slot_ptr[j];
      printf("%lu : %d %d %d\n", slot->tid, slot->num_requests, slot->num_static,
      slot->num_dynamic);
    }
    i++;
  }
}
