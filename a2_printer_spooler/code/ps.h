#ifndef _INCLUDE_PS_H_
#define _INCLUDE_PS_H_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h> 

#include <semaphore.h>

#define MY_SHM "/Feng"

typedef struct {
    sem_t empty;
    sem_t full;
    sem_t mutex;
    int clientindex;
    int printerindex;
    int size;
    int arr[0];
} Shared;

#endif 