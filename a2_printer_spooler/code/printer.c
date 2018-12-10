#include "ps.h"

int r;
int jobpages;
int jobID;
int position;
int temp;
int slot;
int val;

Shared* shared_mem;


//create shared memory
int setup_shared_mem(){
    r = shm_open(MY_SHM, O_CREAT | O_RDWR, 0666);
    if(r == -1){   
    	printf("shm_open() failed\n");
        exit(1);
    }
    ftruncate(r, sizeof(Shared) + 2*slot*sizeof(int));
}

// unlink shared memory
int unlink_shared_mem(){
    r = shm_unlink(MY_SHM);
    if(r == -1){
        printf("shm_unlink() failed\n");
        exit(1);
    }
}

//attach the shared memory
int attach_shared_mem(){
    shared_mem = (Shared*)  mmap(NULL, sizeof(Shared) + 2*slot*sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, r, 0);
    if(shared_mem == MAP_FAILED){
        printf("mmap() failed\n");
        exit(1);
    }
    return 0;
}

//unattach the shared memory
int unattach_shared_mem(){
    int i = munmap(shared_mem, sizeof(Shared));
    if(i == -1){
        printf("munmap() failed\n");
        exit(1);
    }
    return 0;
}

//initialize variables in shared memory
int init_shared_mem() {
    shared_mem -> clientindex = 0;
    shared_mem -> printerindex = 0;
    shared_mem -> size = slot;
    sem_init(&(shared_mem -> full), 1, 0);
    sem_init(&(shared_mem -> empty), 1, slot);
    sem_init(&(shared_mem -> mutex), 1, 1);
    int i;
    for (i=0; i < 2*slot; i++) {
        shared_mem->arr[i] = -1;
    }
}

//if there is no job, print the block
void take_a_job(){
    sem_getvalue(&(shared_mem->full),&val);
    if(val <= 0){ 
        printf("=============== No request in buffer, printer sleeps ===============\n");
    }
    sem_wait(&(shared_mem->full));
    sem_wait(&(shared_mem->mutex));
    position = shared_mem->printerindex;
    jobID = shared_mem->arr[position];    
    jobpages = shared_mem->arr[position+slot];    
    sem_post(&(shared_mem->mutex));
}

//print message
void print_a_msg(){
    printf("Printer starts printing %d pages from client %d from buffer[%d]\n", jobpages,jobID,position);
}

//sleep for job duration
void go_sleep(){
    sleep(jobpages);
    if(shared_mem->printerindex == slot-1){
        shared_mem->printerindex = 0;
    }
    else{
        shared_mem->printerindex = shared_mem -> printerindex+1;
    }
    sem_post(&(shared_mem -> empty));
    printf("Printer finished printing %d pages\n", jobpages);
}

//if user exits by ctrl+c, release the shared memory
void ctrlc(int sig){ 
    unattach_shared_mem();
    unlink_shared_mem();
    close(r);
    printf("\nExit the printer server, shared memory is released.\n");
    exit(0);
} 

int main(int argc, char *argv[]) {
	//check the command
    if(argc > 2){
        printf("arguments exceed!\n");
        exit(-1);
    }
    else if(argc < 2){
        printf("arguments required!\n");
        exit(-1);
    }
    slot = atoi(argv[1]);
    if(slot <=0){
        printf("Invalid slot number!\n");
        exit(-1);
    }

    setup_shared_mem(); 
    attach_shared_mem();
    init_shared_mem();
    while (1) {
        signal(SIGINT,ctrlc);
        take_a_job();
        print_a_msg();
        go_sleep();       
    }
    return 0;
}