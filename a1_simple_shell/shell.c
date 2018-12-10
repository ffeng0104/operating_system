
#include <stdio.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/wait.h>


size_t MAXLINE = 64;

struct Command *head;
struct Command *tail;
struct Command{
    struct Command *next;
    int num,bg,argCount,error,pid,badHC;
    char args[10][64];
};

/* Initialization */
int initial(){
    struct Command *HeadOfList = (struct Command*)malloc(sizeof(struct Command));
    HeadOfList->next = NULL;
    head = HeadOfList;
    tail = HeadOfList;
    head->num = 0;
    return 0;
}



/* Give a command number, return pointer to that command */
struct Command * getCommand(int number){
    struct Command *current = head;
    
    while (current != tail) {
        if (current->num == number) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}


/* Print the entire command on same line */
int printCommand(struct Command *cmd){
    int i=0;
    while (strcmp(cmd->args[i],"")!= 0) {
        printf("%s ",cmd->args[i]);
        i++;
    }
    return 0;
}

/* List all running jobs */
struct Command * listJobs(){
    struct Command *current = head;

    while (current != tail) {
        if (current->bg == 0) {
            if(kill(current->pid, 0)==0) {
                printf("PID: %d \t",getppid());
                printCommand(current);
                printf("\n");
               
             }
        }
        current = current->next;
    }
         return NULL;
    
}

struct Command * fg(int processID){
    struct Command *current = head;
    
    while (current != tail) {
        if (current->bg == 1) {
            if(current->pid==processID) {
                current->bg = 0;
                signal(SIGTTOU, SIG_IGN);                 
                pid_t pid= (pid_t)processID;
                tcsetpgrp(STDIN_FILENO, pid);
                current = tail;
                return NULL;
            }
        }
        current = current->next;
    }
    printf("This process is not running in the background\n");
    return NULL;
}


/* Show if a string is numeric */
int isNumeric (const char * s)
{
    if ( *s == '\0' || s == NULL || isspace(*s))
        return 0;
    char * p;
    strtod (s, &p);
    return *p == '\0';
}

/* Converts a string to a number */
long convertString(char * num){
    char *ptr;
    return strtol(num, &ptr, 10);
}


int addToHistory(struct Command *cmd){

    cmd->next = head;
    head = cmd; 
    
    return 0;
}

/* Free structs that represent particular commands */
int freecmd(struct Command *cmd){
    free(cmd);
    return 0;
}

int exitShell(){
    exit(0);
}

struct Command * getCmd(){
    char *token, *loc;
    //Create a struct for command, allocate memory for it
    struct Command *cmd = (struct Command*)malloc(sizeof(struct Command));
    for (int i=0; i<10; i++) {
        strcpy(cmd->args[i], "");
    }
    
    printf("Shell>>");
    char *line = (char *) malloc (MAXLINE+1);
    int length = (int)getline(&line, &MAXLINE, stdin);
    if (length <= 0) {
        exit(-1);
    }
    
    // Check if bg is specified
    if ((loc = index(line, '&')) != NULL) {
        cmd->bg = 1;
        *loc = ' ';
    } else
        cmd->bg = 0;
    
    //Analyze the line that user entered
    int argCount = 0;
    while ((token = strsep(&line, " \t\n")) != NULL && argCount < 10) {
        for (int j = 0; j < strlen(token); j++)
            if (token[j] <= 32)
                token[j] = '\0';
        if (strlen(token) > 0){
            strcpy(cmd->args[argCount], token);
            argCount++;
        }
        
    }
    
    //If the command starts with "!", find the number in history and run
     if (strcmp(cmd->args[0], "!") == 0) {        
        if (isNumeric(cmd->args[1])) {
            int num = (int)convertString(cmd->args[1]);
            struct Command *AssocCmd = getCommand(num);
            if (AssocCmd != NULL) {
                for (int l=0; l<10; l++) {
                    strcpy(cmd->args[l],AssocCmd->args[l]);
                }
                
                cmd->argCount = AssocCmd->argCount;
                cmd->num = head->num+1;
                cmd->bg = AssocCmd->bg;
                cmd->error = AssocCmd->error;
                
            }else{
                cmd->badHC = 1;
            }
        
        }
    }else{
        cmd->argCount = argCount;
        cmd->num = head->num+1;
    }

    return cmd;
}


//Print current directory
int pwd(){
    char buffer[100];
    size_t buffersize = 100;
    getcwd(buffer, buffersize);
    printf("Current Work Directory is: %s\n",buffer);
    return 0;
}

int redirect(struct Command *cmd) {
    for(int i=0; i<64; i++){
        if((strcmp(cmd -> args[i],">"))==0){
            return i;
        }         
    }
    return -1;
}

int lsFile(struct Command *cmd, int loc) {
    printf("list to a file \n");
    char *command = (char *)malloc(64);
    char *ls_cmd[64] = {NULL};
    for(int i=0; i<(cmd->argCount); i++){
        ls_cmd[i] = malloc(sizeof(command));
        strcpy(ls_cmd[i],cmd->args[i]); 
    }
    
    if(fork()==0) {
        close(1);
        int fd= open(ls_cmd[loc+1],O_RDWR| O_CREAT, S_IRUSR| S_IWUSR);
        ls_cmd[loc]=NULL;
        ls_cmd[loc+1]=NULL;
        if((strcmp(ls_cmd[0], "pwd"))==0) {
            pwd();
        }
        else{
            execvp(ls_cmd[0], ls_cmd);
        }
    }
    return 0;
}

//Change directory
int cd(char *arg){
    if(chdir(arg)<0){
        printf("Failed to change directory\n");
        return 1;
    }
    return 0;
}

/* Print last 10 lines */
int printHistory(){
    struct Command *node = head;
    
    printf("\n-------History-------\n");
 
    for (int i = 0; i<10; i++) {
        if(node->next == NULL)
            break;
        printf("%d\t",node->num);
        printCommand(node);
        printf("\n");
        
        node=node->next;
    }   
    return 0;
}

/* Run command args in child process */
int runChild(struct Command *cmd){
    int pid,status;
    
    if ((pid = fork())<0) {
        printf("Failed to fork a child process, shell exit\n");
        exitShell();
    }else if (pid == 0){
        char *command = (char *)malloc(64);
        char *rc[64] = {NULL};
        for(int i=0; i<(cmd->argCount); i++){
            rc[i] = malloc(sizeof(command));
            strcpy(rc[i],cmd->args[i]);
        }
        if ((execvp(rc[0], rc)) < 0) {
            printf("Invalid command, failed to run\n");
        }
        exit(0); 
    }else{
        // In Parent Process
        if(cmd->bg == 0){
            while (wait(&status) != pid);
        }else{
            cmd->pid = pid;
        }
    }
    
    return 0;
}



/* Run the command */
int runCmd(struct Command *cmd){

    int location=redirect(cmd);
    
    if (strcmp(cmd->args[0], "history") == 0) {
        printHistory();
    }else if (strcmp(cmd->args[0], "cd") == 0) {
        cd(cmd->args[1]);
    }
    else if (strcmp(cmd->args[0], "pwd") == 0 && location <0) {
        pwd();
    }
    else if (strcmp(cmd->args[0], "exit") == 0) {
        exitShell();
    }else if (strcmp(cmd->args[0], "fg") == 0) {
        if(isNumeric(cmd->args[1])==1){
            fg((int)convertString(cmd->args[1]));
        }else{
            printf("Invalid number");
        }
    }else if (strcmp(cmd->args[0], "jobs") == 0) {
        listJobs();
    }else if (location>=0) {
        lsFile(cmd, location); 
    }else{
        if (cmd->error != 1 && cmd->badHC != 1) {
            printf("Running the cmd ");
            for (int i=0; i<cmd->argCount; i++) {
                printf("%s ",cmd->args[i]);
            }
            printf("\n");

           
            runChild(cmd);
            
        }else{
            //Mention that command is failed
            if(cmd->badHC != 1){
                printf("The command: ");
                printCommand(cmd);
                printf(" failed to execute, it's not added to history");
                
            }else{
                printf("No command found in history\n");
            }
            
        }
        
    }
    
    //If the command executed successfully, add it to history
    if (cmd->error != 1 && strcmp(cmd->args[0],"history")!= 0 && cmd->badHC !=1) {
        addToHistory(cmd);
    }else{
        freecmd(cmd);
    }
    
    return 0;
}

int main(){
    initial();
    struct Command *currentCmd;
    while (1) {
        //get the command and run it
        currentCmd = getCmd();        
        runCmd(currentCmd);
        
    }
}