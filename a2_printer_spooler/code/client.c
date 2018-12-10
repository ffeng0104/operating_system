#include "ps.h"

int r;
int size;
int ID;
int pages;
int temp;
int wait = 0;
int val;

Shared* shared_mem;

//open shared memory
int setup_shared_mem(){
    r = shm_open(MY_SHM, O_RDWR, 0666);
    if(r == -1){
        printf("shm_open() failed\n");
        exit(1);
    }
}

//attach shared memory
int attach_shared_mem(){
    shared_mem = (Shared*) mmap(NULL, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED, r, 0);
    if(shared_mem == MAP_FAILED){
        printf("mmap() failed\n");
        exit(1);
    }
    return 0;
}

//unattach shared memory
int unattach_shared_mem(){
    int i = munmap(shared_mem, sizeof(Shared));
    if(i == -1){
        printf("munmap() failed\n");
        exit(1);
    }
    return 0;
}

//get job parameters from terminal
void get_job_params(int argc, char *argv[]){
    pages = atoi(argv[2]);
    ID = atoi(argv[1]);
}

//put a job into shared memory
void put_a_job(){
    sem_getvalue(&(shared_mem->empty),&val);
    //if buffer is full, wait for an empty slot
    if(val <= 0){
        wait = 1;
        printf("Client %d has %d pages to print, buffer is currently full, sleeps\n",ID,pages);
    }
    sem_wait(&(shared_mem->empty));
    sem_wait(&(shared_mem->mutex));
    temp = shared_mem->clientindex;
    //update the index
    if(shared_mem->clientindex == shared_mem->size-1){
        shared_mem->clientindex = 0; 
    }
    else{
        shared_mem->clientindex = shared_mem->clientindex+1;
    }
    //save parameters
    shared_mem -> arr[temp] = ID;
    shared_mem -> arr[temp+size] = pages;
    if(wait == 0){
        printf("Client %d has %d pages to print, put request in buffer[%d]\n", ID,pages,temp);
    }
    else if(wait == 1){
        printf("There is a space in buffer now, client %d wakes up, put request in buffer[%d]\n", ID,temp);
    }
    sem_post(&(shared_mem->mutex));
    sem_post(&(shared_mem->full));
}

int main(int argc, char *argv[]) {
    if(argc >3){
        printf("arguments exceed!\n");
        exit(-1);
    }
    else if(argc <3){
        printf("arguments required!\n");
        exit(-1);
    }
    setup_shared_mem();
    attach_shared_mem();
    size = shared_mem -> size;
    get_job_params(argc,argv); 
    put_a_job(); 
    unattach_shared_mem();
    close(r);
    return 0;
}