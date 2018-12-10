#include "sfs_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  
#include <libgen.h>
#include "disk_emu.h"
#include <stdint.h> 

#define MAXFILENAME 20
#define MAX_FILE_NUM 99

int seen = 0;

#define OS_DISK "sfs_disk.disk"
#define BLOCK_SZ 1024 
#define NUM_INODES 10 
#define NUM_BLOCKS 1024
#define SIZE (NUM_BLOCKS/8) 

#define NUM_INODE_BLOCKS (sizeof(inode_t) * NUM_INODES / BLOCK_SZ + 1)
#define NUM_OF_NORMAL_BLOCKS (NUM_BLOCKS -1 - NUM_INODE_BLOCKS)
#define NUM_OF_ROOT_BLOCKS (sizeof(root_dir)*NUM_INODES/BLOCK_SZ +1)

uint8_t free_bit_map[SIZE] = { [0 ... SIZE-1] = UINT8_MAX };

#define FREE_BIT(_data, _which_bit) \
    _data = _data | (1 << _which_bit)

#define USE_BIT(_data, _which_bit) \
    _data = _data & ~(1 << _which_bit)

void force_set_index(uint32_t index) {
    uint32_t i = index / 8;
    uint8_t bit = index % 8;
    USE_BIT(free_bit_map[i], bit);
}

uint32_t get_index() {
    uint32_t i = 0;

    while (free_bit_map[i] == 0) { 
        i++; 
    }

    uint8_t bit = ffs(free_bit_map[i]) - 1;
    USE_BIT(free_bit_map[i], bit);
    return i*8 + bit;
}

void rm_index(uint32_t index) {
    uint32_t i = index / 8;
    uint8_t bit = index % 8;
    FREE_BIT(free_bit_map[i], bit);
}

typedef struct {
    uint64_t magic;
    uint64_t block_size;
    uint64_t fs_size;
    uint64_t inode_table_len;
    uint64_t root_dir_inode;
} superblock_t;

typedef struct {
    unsigned int mode;
    unsigned int link_cnt;
    unsigned int uid;
    unsigned int gid;
    int size;
    int data_ptrs[12];
    int used;
    int indirectptr;
} inode_t;


typedef struct {
    uint64_t inode;
    uint64_t rwptr;
    uint64_t wptr;
    int open;
} file_descriptor;

typedef struct 
{
    int inoteindex;
    char *filename;
}root_dir;

superblock_t sb;
inode_t table[NUM_INODES];
root_dir root[NUM_INODES];
uint8_t free_bit_map[BLOCK_SZ/8];
file_descriptor fdt[NUM_INODES];
int position;
int nextfile = 0;

void init_superblock() {
    sb.magic = 0xACBD0005;
    sb.block_size = BLOCK_SZ;
    sb.fs_size = NUM_BLOCKS * BLOCK_SZ;
    sb.inode_table_len = NUM_INODE_BLOCKS;
    sb.root_dir_inode = 0;
}

//init functions
void init_inode_table(){
    int i;
    int j;
    for(i = 0;i<NUM_INODES;i++){
        for(j=0;j<12;j++){
            table[i].data_ptrs[j] = 0;
            table[i].used = 0;
            table[i].indirectptr = 0;
        }
    }
}
void init_free_bit_map(){
    int i;
    for(i=0;i<BLOCK_SZ/8;i++){
        free_bit_map[i] = 0xff;
    }
}
void init_root_directory(){
    int i;
    for(i=0;i < NUM_INODES;i++){
        root[i].filename ="\0";
        root[i].inoteindex = NUM_INODES+1;
    }
}
void init_file_descriptor(){
    int i;
    for(i=0;i<NUM_INODES;i++){
        fdt[i].inode = NUM_INODES+1;
        fdt[i].rwptr = 0;
        fdt[i].wptr = 0;
        fdt[i].open = 0;
    }
}

//update freebitmap to disk
void updatefreebitmap(){
    write_blocks(NUM_INODE_BLOCKS+1,1,free_bit_map);
}
//update inode table to disk
void updatetable(){
    write_blocks(1, sb.inode_table_len, table);
}
//update root directory to disk
void updateroot(){
    write_blocks(NUM_INODE_BLOCKS+2,NUM_OF_ROOT_BLOCKS,root);
}

void mksfs(int fresh){
  int i;
    if (fresh) {	
        printf("making new file system\n");
        init_superblock();
        init_inode_table();   
        init_free_bit_map();
        init_fresh_disk(OS_DISK, BLOCK_SZ, NUM_BLOCKS);
        
        write_blocks(0, 1, &sb);
        write_blocks(1, sb.inode_table_len, table);
        write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
        write_blocks(NUM_INODE_BLOCKS+2,NUM_OF_ROOT_BLOCKS,root);
        
        for(i =0;i<(NUM_INODE_BLOCKS+2+NUM_OF_ROOT_BLOCKS);i++){
            force_set_index(i);
        }
        updatefreebitmap();
        //initial root directory and file descriptor
        init_root_directory();
        init_file_descriptor();
        write_blocks(NUM_INODE_BLOCKS+2,NUM_OF_ROOT_BLOCKS,root);
        int i;
        
        for(i=0;i<NUM_OF_ROOT_BLOCKS;i++){
            table[0].data_ptrs[i] = NUM_INODE_BLOCKS+2+i;
            table[0].used =1;
        }
        updatetable();
        position = -1;
    } else {
        position = -1;
        printf("reopening file system\n");
        //open super block
        init_disk(OS_DISK,BLOCK_SZ,NUM_BLOCKS);
        read_blocks(0, 1, &sb);
        printf("Block Size is: %lu\n", sb.block_size);
        //open inode table,freebitmap and rootdirectory
        read_blocks(1, sb.inode_table_len, table);
        read_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
        read_blocks(NUM_INODE_BLOCKS+2,NUM_OF_ROOT_BLOCKS,root);
    }
	return;
}

int sfs_get_next_file_name(char *fname){
  int i; 
    for (i = nextfile; i < NUM_INODES; i++){
        if (strcmp(root[i].filename, "\0") != 0){
            //copy filename to fname with max file length
            strncpy(fname, root[i].filename, MAXFILENAME); 
            nextfile ++;   
            return 1; 
        }
    }
    return 0;
}

int sfs_get_file_size(char* path){
    int i;
    int inodeindex;
    char* name = basename((char*)path);
    //get the size of file
    for(i=0;i<NUM_INODES;i++){
        if(strcmp(root[i].filename,name)==0){
            inodeindex = root[i].inoteindex;
            return table[inodeindex].size;
            break;
        }
    }
	return -1;
}

int sfs_fopen(char *name){
  int i;
    int created = 0;
    if(strlen(name)>MAXFILENAME){
        return -1;
    }
    //check for same name in root directory
    for(i=0;i<NUM_INODES;i++){
        if(strncmp(name,root[i].filename,MAXFILENAME)==0){
            created = 1;
            int j;
            for(j=0;j<NUM_INODES;j++){
                if(fdt[j].inode ==root[i].inoteindex){
                    if(fdt[j].open ==1){
                        return j;
                    }
                    else{
                        fdt[j].open = 1;
                        fdt[j].rwptr = 0;
                        fdt[j].wptr = table[fdt[j].inode].size;
                        return j;
                    }
                }
            }
        }
    }
    //if cannot find the file then create one
    if(created==0){
        for(i=0;i<NUM_INODES;i++){
            //find the first availiable slot
            if(strlen(root[i].filename) ==0 ){
                int j;
                for(j=0;j<NUM_INODES;j++){
                    //find the first unused slot
                    if(table[j].used ==0){
                        //init
                        table[j].used =1;
                        table[j].size = 0;
                        root[i].inoteindex = j;
                        root[i].filename = name;
                        updateroot();
                        break;
                    }
                }
                int k;
                for(k=0;k<NUM_INODES;k++){
                    //find the first unused slot in fdt
                    if(fdt[k].inode == NUM_INODES+1){
                        fdt[k].inode = j;
                        fdt[k].rwptr = 0;
                        fdt[k].open = 1;
                        fdt[k].wptr = 0;
                        return k;
                    }
                }

            }
        }
    }
    return -1;
}

int sfs_fclose(int fileID){
  if(fileID < 0){
        printf("this file does not exist!\n");
	    return -1;
  }
  if(fdt[fileID].open ==1){	
        fdt[fileID].open =0;
        return 0;
    }
    else{
        printf("this file has been closed before!\n");
	    return -1;
    }
}

int sfs_frseek(int fileID, int loc){
  int index = fdt[fileID].inode;
    int size = table[index].size;
    if(size<loc){
        loc = size;
    }
    fdt[fileID].rwptr = loc;
	return 0;
}

int sfs_fwseek(int fileID, int loc){
  int index = fdt[fileID].inode;
    int size = table[index].size;
    if(size<loc){
        loc = size;
    }
    fdt[fileID].wptr = loc;
	return 0;
}

int sfs_fwrite(int fileID, char *buf, int length){
    int inodeindex = fdt[fileID].inode;
    int pointer = fdt[fileID].wptr;
    //reset the num bytes to 0
    int num_bytes_to_be_written = 0;
    int numblkwr;
    int offset = pointer % BLOCK_SZ;

    if((offset + length) < BLOCK_SZ){
        numblkwr = 1;
    }
    else{
        numblkwr = 2+(length-(BLOCK_SZ-offset))/BLOCK_SZ;
    }
    //if the file hasn't been writen before
    if(table[inodeindex].data_ptrs[0] == 0){
        if(numblkwr <=12){
            char write_temp[BLOCK_SZ*2];
            int j;
            for(j=0;j<numblkwr-1;j++){
                //reset temp buffer
                memset(write_temp,0,BLOCK_SZ*2);
                memcpy(write_temp,buf+j*BLOCK_SZ,BLOCK_SZ);
                table[inodeindex].data_ptrs[j] = get_index();
                updatetable();
                write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
                int write = write_blocks(table[inodeindex].data_ptrs[j],1,write_temp);
                if(write >= 1){
                    num_bytes_to_be_written += BLOCK_SZ;
                    fdt[fileID].wptr +=BLOCK_SZ;
                }
            }
            memset(write_temp,0,BLOCK_SZ*2);
            memcpy(write_temp,buf+(numblkwr-1)*BLOCK_SZ, length%BLOCK_SZ);
            table[inodeindex].data_ptrs[numblkwr-1] = get_index();
            updatetable();
            write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
            int write = write_blocks(table[inodeindex].data_ptrs[numblkwr-1],1,write_temp);
            if(write>=1){
                num_bytes_to_be_written += (length%BLOCK_SZ);
                fdt[fileID].wptr +=(length%BLOCK_SZ);
            }
        
        table[inodeindex].size += num_bytes_to_be_written;
        updatetable();
        //update inode table and freebitmap to disk
        write_blocks(1, sb.inode_table_len, table);
        write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
        return num_bytes_to_be_written;
    }

        //if more than 12 blocks, we need to use indirect pointer
        else{
            int j;
            char write_temp[BLOCK_SZ];
            int indirectblknum = numblkwr-12;
            //same things as before to write first 12 blocks
            for(j=0;j<12;j++){
                memset(write_temp,0,BLOCK_SZ*2);
                memcpy(write_temp,buf+j*BLOCK_SZ,BLOCK_SZ);
                table[inodeindex].data_ptrs[j] = get_index();
                updatetable();
                write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
                int write = write_blocks(table[inodeindex].data_ptrs[j],1,write_temp);
                if(write>=1){
                    num_bytes_to_be_written += BLOCK_SZ;
                    fdt[fileID].wptr +=BLOCK_SZ;
                }
            }
            //get free block to store indirect pointer
            table[inodeindex].indirectptr = get_index();
            updatetable();
            updatefreebitmap();
            write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
            unsigned int indirectptrarray[BLOCK_SZ/sizeof(unsigned int)];
            //write data to middle blocks
            for(j=0;j<indirectblknum-1;j++){
                indirectptrarray[j] = get_index();
                write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
                memset(write_temp,0,BLOCK_SZ*2);
                memcpy(write_temp,buf+(12+j)*BLOCK_SZ,BLOCK_SZ);
                int write = write_blocks(indirectptrarray[j],1,write_temp);
                if(write>=1){
                    num_bytes_to_be_written +=BLOCK_SZ;
                    fdt[fileID].wptr +=BLOCK_SZ;
                }
            }
            indirectptrarray[indirectblknum-1] = get_index();
            write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
            memset(write_temp,0,BLOCK_SZ*2);
            //write data to the last block
            memcpy(write_temp,buf+(numblkwr-1)*BLOCK_SZ,length%BLOCK_SZ);
            int write = write_blocks(indirectptrarray[indirectblknum-1],1,write_temp);
            if(write>=1){
                num_bytes_to_be_written += (length%BLOCK_SZ);
                fdt[fileID].wptr +=(length%BLOCK_SZ);
            }

            //update to disk
            table[inodeindex].size += num_bytes_to_be_written;
            write_blocks(table[inodeindex].indirectptr,1,indirectptrarray);
            write_blocks(1, sb.inode_table_len, table);
            write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
            return num_bytes_to_be_written;
        }
    }

    else{        
        int write; 
        int currentblk = pointer/BLOCK_SZ +1;
        int endblk = (pointer+length)/BLOCK_SZ +1;
        char write_temp[BLOCK_SZ];
        if(endblk <=12){
            if(endblk>currentblk){
                //write data to the first block
                memset(write_temp,0,BLOCK_SZ*2);
                read_blocks(table[inodeindex].data_ptrs[currentblk-1],1,write_temp);
                memcpy(write_temp+offset,buf,BLOCK_SZ-offset);
                write = write_blocks(table[inodeindex].data_ptrs[currentblk-1],1,write_temp);
                if(write >=1){
                    num_bytes_to_be_written +=(BLOCK_SZ - offset);
                    fdt[fileID].wptr +=(BLOCK_SZ-offset);
                }
                int j;
                //write data to middle blocks
                for(j=currentblk;j<endblk-1;j++){
                    memset(write_temp,0,BLOCK_SZ*2);
                    memcpy(write_temp,buf+BLOCK_SZ-offset+(j-currentblk)*BLOCK_SZ,BLOCK_SZ);
                    if(table[inodeindex].data_ptrs[j] == 0){
                        table[inodeindex].data_ptrs[j] = get_index();
                        updatetable();
                        write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
                    }
                    write = write_blocks(table[inodeindex].data_ptrs[j],1,write_temp);
                    if(write>=1){
                        num_bytes_to_be_written+=BLOCK_SZ;
                        fdt[fileID].wptr +=BLOCK_SZ;
                    }
                }
                //write data to the last block
                memset(write_temp,0,BLOCK_SZ*2);
                memcpy(write_temp,buf+(endblk-currentblk)*BLOCK_SZ-offset,(offset+length)%BLOCK_SZ);
                if(table[inodeindex].data_ptrs[endblk-1] == 0){
                    table[inodeindex].data_ptrs[endblk-1] = get_index();
                    updatetable();
                    write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
                }
                write = write_blocks(table[inodeindex].data_ptrs[endblk-1],1,write_temp);
                if(write>=1){
                    num_bytes_to_be_written += ((length+offset)%BLOCK_SZ);
                    fdt[fileID].wptr +=((length+offset)%BLOCK_SZ);
                }
                //update size
                if(length+pointer > table[inodeindex].size){
                    table[inodeindex].size = length+pointer;
                    updatetable();
                }
                //update freebitmap and inode table
                write_blocks(1, sb.inode_table_len, table);
                write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
                return num_bytes_to_be_written;
            }
           
            else if(currentblk == endblk){
                memset(write_temp,0,BLOCK_SZ*2);
                read_blocks(table[inodeindex].data_ptrs[currentblk-1],1,write_temp);
                memcpy(write_temp+offset,buf,length);
                write = write_blocks(table[inodeindex].data_ptrs[currentblk-1],1,write_temp);
                if(write >=1){
                    num_bytes_to_be_written +=length;
                    fdt[fileID].wptr +=length;
                }
                if(length+pointer > table[inodeindex].size){
                    table[inodeindex].size = length+pointer;
                    updatetable();
                }
                return num_bytes_to_be_written;
            }

        }
        else if(currentblk > 12){
            if(currentblk < endblk){
                //write data on the first block
                memset(write_temp,0,BLOCK_SZ*2);
                unsigned int indirectptrarray2[BLOCK_SZ/sizeof(unsigned int)];
                read_blocks(table[inodeindex].indirectptr,1,indirectptrarray2);
                read_blocks(indirectptrarray2[currentblk-13],1,write_temp);
                memcpy(write_temp+offset,buf,BLOCK_SZ-offset);
                write = write_blocks(indirectptrarray2[currentblk-13],1,write_temp);
                if(write >=1){
                    num_bytes_to_be_written +=(BLOCK_SZ-offset);
                    fdt[fileID].wptr += (BLOCK_SZ -offset);
                }
                int j;
                for(j=currentblk;j<endblk-1;j++){
                    //write data on middle blocks
                    if(indirectptrarray2[j-12] == 0){
                        indirectptrarray2[j-12] =get_index();
                        write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
                    }
                    memset(write_temp,0,BLOCK_SZ*2);
                    memcpy(write_temp,(buf+BLOCK_SZ-offset+(j-currentblk)*BLOCK_SZ),BLOCK_SZ);
                    write=write_blocks(indirectptrarray2[j-12],1,write_temp);
                    if(write >=1){
                        num_bytes_to_be_written += BLOCK_SZ;
                        fdt[fileID].wptr += BLOCK_SZ;
                    }
                }
                //write data on the last block
                memset(write_temp,0,BLOCK_SZ*2);
                memcpy(write_temp,(buf+(endblk-currentblk)*BLOCK_SZ-offset),(offset+length)%BLOCK_SZ);
                if(indirectptrarray2[endblk-13] == 0){
                    indirectptrarray2[endblk-13] =get_index();
                    write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
                }
                write = write_blocks(indirectptrarray2[endblk-13],1,write_temp);
                if(write >=1){
                    num_bytes_to_be_written +=(offset+length)%BLOCK_SZ;
                    fdt[fileID].wptr +=(offset+length)%BLOCK_SZ;
                }
                if(length+pointer > table[inodeindex].size){
                    table[inodeindex].size = length+pointer;
                    updatetable();
                }
                //update freebitmap and inode table
                write_blocks(table[inodeindex].indirectptr,1,indirectptrarray2);
                write_blocks(1, sb.inode_table_len, table);
                write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
                return num_bytes_to_be_written;
            }
            else if(endblk == currentblk){
                //write data on current block
                memset(write_temp,0,BLOCK_SZ*2);
                unsigned int indirectptrarray2[BLOCK_SZ/sizeof(unsigned int)];
                read_blocks(table[inodeindex].indirectptr,1,indirectptrarray2);
                read_blocks(indirectptrarray2[currentblk-13],1,write_temp);
                memcpy(write_temp+offset,buf,length);
                write = write_blocks(indirectptrarray2[currentblk-13],1,write_temp);
                if(write >=1){
                    num_bytes_to_be_written +=length;
                    fdt[fileID].wptr += length;
                }
                if(length+pointer > table[inodeindex].size){
                    table[inodeindex].size = length+pointer;
                    updatetable();
                }
                //update freebitmap and inode table
                write_blocks(table[inodeindex].indirectptr,1,indirectptrarray2);
                write_blocks(1, sb.inode_table_len, table);
                write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
                return num_bytes_to_be_written;
            }
        }
        else if(currentblk<=12 && endblk>12){
                unsigned int indirectptrarray3[BLOCK_SZ/sizeof(unsigned int)];
                memset(write_temp,0,BLOCK_SZ*2);
                read_blocks(table[inodeindex].data_ptrs[currentblk-1],1,write_temp);
                memcpy(write_temp+offset,buf,BLOCK_SZ-offset);
                //write data to the first block
                write = write_blocks(table[inodeindex].data_ptrs[currentblk-1],1,write_temp);
                if(write >=1){
                    num_bytes_to_be_written +=(BLOCK_SZ - offset);
                    fdt[fileID].wptr +=(BLOCK_SZ-offset);
                }
                int j;
                //write data on middle blocks
                for(j=currentblk;j<12;j++){
                    memset(write_temp,0,BLOCK_SZ*2);
                    memcpy(write_temp,(buf+BLOCK_SZ-offset+(j-currentblk)*BLOCK_SZ),BLOCK_SZ);
                    if(table[inodeindex].data_ptrs[j] == 0){
                        table[inodeindex].data_ptrs[j] = get_index();
                        updatetable();
                        write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
                    }
                    write = write_blocks(table[inodeindex].data_ptrs[j],1,write_temp);
                    if(write>=1){
                        num_bytes_to_be_written+=BLOCK_SZ;
                        fdt[fileID].wptr +=BLOCK_SZ;
                    }
                }
                
                for(j=12;j<endblk-1;j++){
                    if(table[inodeindex].indirectptr == 0){
                        table[inodeindex].indirectptr = get_index();
                        updatetable();
                        write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
                        read_blocks(table[inodeindex].indirectptr,1,indirectptrarray3);
                    }

                    if(indirectptrarray3[j-12] == 0){
                        indirectptrarray3[j-12] =get_index();
                        write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
                    }
                    memset(write_temp,0,BLOCK_SZ*2);
                    memcpy(write_temp,(buf+BLOCK_SZ-offset+(j-currentblk)*BLOCK_SZ),BLOCK_SZ);
                    write=write_blocks(indirectptrarray3[j-12],1,write_temp);
                    if(write >=1){
                        num_bytes_to_be_written += BLOCK_SZ;
                        fdt[fileID].wptr += BLOCK_SZ;
                    }
                }
                //write data on the last block
                memset(write_temp,0,BLOCK_SZ*2);
                memcpy(write_temp,(buf+(endblk-currentblk)*BLOCK_SZ-offset),(offset+length)%BLOCK_SZ);
                if(indirectptrarray3[endblk-13] == 0){
                    indirectptrarray3[endblk-13] =get_index();
                    write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
                }
                write = write_blocks(indirectptrarray3[endblk-13],1,write_temp);
                if(write >=1){
                    num_bytes_to_be_written +=((offset+length)%BLOCK_SZ);
                    fdt[fileID].wptr +=((offset+length)%BLOCK_SZ);
                }
                //update the file size
                if(length+pointer > table[inodeindex].size){
                    table[inodeindex].size = length+pointer;
                    updatetable();
                }
                //update inode table and freebitmap
                write_blocks(table[inodeindex].indirectptr,1,indirectptrarray3);
                write_blocks(1, sb.inode_table_len, table);
                write_blocks(1+NUM_INODE_BLOCKS,1,free_bit_map);
                return num_bytes_to_be_written;
        }
    }
}

int sfs_fread(int fileID, char *buf, int length){
   if(fdt[fileID].open == 0){
        return 0;
    }
    else{
    int inodeindex = fdt[fileID].inode;
    int pointer = fdt[fileID].rwptr;
    int size = table[inodeindex].size;
    
    if(length+pointer>size){
        length = size - pointer;
    }

    int num_bytes_to_be_read = 0;
    int numblkrd;
    if(1){
        //current block of read pointer
        int currentblk = pointer/BLOCK_SZ +1;
        //last block that will be read
        int endblk = (pointer+length)/BLOCK_SZ +1;
        int offset = pointer%BLOCK_SZ;
        //how many blocks will be read
        if((offset + length) < BLOCK_SZ){
            numblkrd = 1;
        }
        else{
            numblkrd = 2+(length-(BLOCK_SZ-offset))/BLOCK_SZ;
        }
        //create a temp buffer to store data
        char read_temp[BLOCK_SZ*2];

        if(endblk <=12){
            if(endblk > currentblk){
                //reset temp buffer
                memset(read_temp,0,BLOCK_SZ*2);
                read_blocks(table[inodeindex].data_ptrs[currentblk-1],1,read_temp);
                memcpy((char*)(buf+num_bytes_to_be_read),read_temp+offset,BLOCK_SZ-offset);
                //update 
                num_bytes_to_be_read +=BLOCK_SZ-offset;
                fdt[fileID].rwptr +=BLOCK_SZ-offset;
                int j;
                for(j=currentblk;j<endblk-1;j++){
                    memset(read_temp,0,BLOCK_SZ*2);
                    read_blocks(table[inodeindex].data_ptrs[j],1,read_temp);
                    memcpy((char*)(buf+num_bytes_to_be_read),read_temp,BLOCK_SZ);
                    num_bytes_to_be_read +=BLOCK_SZ;
                    fdt[fileID].rwptr +=BLOCK_SZ;
                }
                memset(read_temp,0,BLOCK_SZ*2);
                read_blocks(table[inodeindex].data_ptrs[endblk-1],1,read_temp);
                //copy data before end portion
                memcpy((char*)(buf+num_bytes_to_be_read),read_temp,(length+pointer)%BLOCK_SZ);
                num_bytes_to_be_read +=(length+pointer)%BLOCK_SZ;
                fdt[fileID].rwptr +=(length+pointer)%BLOCK_SZ;
                return num_bytes_to_be_read;
            }
            else if(endblk == currentblk){
                memset(read_temp,0,BLOCK_SZ*2);
                read_blocks(table[inodeindex].data_ptrs[currentblk-1],1,read_temp);
                memcpy((char*)(buf),read_temp+offset,length);
                num_bytes_to_be_read +=length;
                fdt[fileID].rwptr +=length;
                return num_bytes_to_be_read;
            }
        }
        
        else if(currentblk>12){
            if(currentblk<endblk){
                memset(read_temp,0,BLOCK_SZ*2);
                unsigned int indirectptrarray[BLOCK_SZ/sizeof(unsigned int)];
                //get indirect pointer array
                read_blocks(table[inodeindex].indirectptr,1,indirectptrarray);
                read_blocks(indirectptrarray[currentblk-13],1,read_temp);
                memcpy((char*)(buf+num_bytes_to_be_read),read_temp+offset,BLOCK_SZ-offset);
                num_bytes_to_be_read +=BLOCK_SZ -offset;
                fdt[fileID].rwptr += BLOCK_SZ-offset;
                int j;
                for(j=currentblk;j<endblk-1;j++){
                    memset(read_temp,0,BLOCK_SZ*2);
                    read_blocks(indirectptrarray[j-12],1,read_temp);
                    memcpy((char*)(buf+num_bytes_to_be_read),read_temp,BLOCK_SZ);
                    num_bytes_to_be_read +=BLOCK_SZ;
                    fdt[fileID].rwptr += BLOCK_SZ;
                }
                memset(read_temp,0,BLOCK_SZ*2);
                read_blocks(indirectptrarray[endblk-13],1,read_temp);
                memcpy((char*)(buf+num_bytes_to_be_read),read_temp,(offset+length)%BLOCK_SZ);
                num_bytes_to_be_read +=(offset+length)%BLOCK_SZ;
                fdt[fileID].rwptr +=(offset+length)%BLOCK_SZ;
                return num_bytes_to_be_read;
            }
            else if(currentblk == endblk){
                memset(read_temp,0,BLOCK_SZ*2);
                unsigned int indirectptrarray2[BLOCK_SZ/sizeof(unsigned int)];
                read_blocks(table[inodeindex].indirectptr,1,indirectptrarray2);
                read_blocks(indirectptrarray2[currentblk-13],1,read_temp);
                memcpy((char*)(buf+num_bytes_to_be_read),read_temp+offset,length);
                num_bytes_to_be_read +=length;
                fdt[fileID].rwptr += length;
                return num_bytes_to_be_read;
            }
        }
        
        else if(currentblk <=12 && endblk > 12){
            //read data on first block
            unsigned int indirectptrarray3[BLOCK_SZ/sizeof(unsigned int)];
            read_blocks(table[inodeindex].indirectptr,1,indirectptrarray3);
            memset(read_temp,0,BLOCK_SZ*2);
            read_blocks(table[inodeindex].data_ptrs[currentblk-1],1,read_temp);
            memcpy((char*)(buf+num_bytes_to_be_read),read_temp+offset,BLOCK_SZ-offset);
            num_bytes_to_be_read += BLOCK_SZ-offset;
            fdt[fileID].rwptr += BLOCK_SZ-offset;
            int j;
            //read data on middle blocks
            for(j=currentblk;j<12;j++){
                memset(read_temp,0,BLOCK_SZ*2);
                read_blocks(table[inodeindex].data_ptrs[j],1,read_temp);
                memcpy((char*)(buf+num_bytes_to_be_read),read_temp,BLOCK_SZ);
                num_bytes_to_be_read +=BLOCK_SZ;
                fdt[fileID].rwptr += BLOCK_SZ;
            }
            for(j=12;j<endblk-1;j++){
                memset(read_temp,0,BLOCK_SZ*2);
                read_blocks(indirectptrarray3[j-12],1,read_temp);
                memcpy((char*)(buf+num_bytes_to_be_read),read_temp,BLOCK_SZ);
                num_bytes_to_be_read +=BLOCK_SZ;
                fdt[fileID].rwptr +=BLOCK_SZ;
            }
            //read data on last block
            memset(read_temp,0,BLOCK_SZ*2);
            read_blocks(indirectptrarray3[endblk-13],1,read_temp);
            memcpy((char*)(buf+num_bytes_to_be_read),read_temp,(offset+length)%BLOCK_SZ);
            num_bytes_to_be_read += (offset+length)%BLOCK_SZ;
            fdt[fileID].rwptr +=(offset+length)%BLOCK_SZ;
            return num_bytes_to_be_read;
        }

        }
    }
}
int sfs_remove(char *file){
  int i,j;
    j = NUM_INODES+1;
    for(i=0;i<NUM_INODES;i++){
        if(strcmp(file,root[i].filename)==0){
            j = i;
        }
    }
    if(j == NUM_INODES+1){
        return -1;
    }
    else{
        //delete info in root dir
        root[j].filename = "\0";
        int remain;
        char emptyblock[BLOCK_SZ/sizeof(char)] = "\0";
        unsigned int indirectptrtemp2[10000] ;
        int index = root[j].inoteindex;
        //delete info in fdt
        for(i = 0;i<NUM_INODES;i++){
            if(fdt[i].inode == index){
                fdt[i].inode = 0;
                fdt[i].rwptr = 0;
                fdt[i].wptr = 0;
                fdt[i].open = 0;
            }
        }
        //delete info in inode
        //delete stored data in disk
        for(i = 0; i < 12; i++){
            if(table[index].data_ptrs[i] != 0){
                write_blocks(table[index].data_ptrs[i],1,emptyblock);
                rm_index(table[index].data_ptrs[i]);
                updatefreebitmap();
                table[index].data_ptrs[i] = 0;
            }
        }
        if(table[index].indirectptr != NUM_BLOCKS +1){
            read_blocks(table[index].indirectptr,1,indirectptrtemp2);
            //printf("table[index].indirectptr:%d indirectptrtemp2[0]:%d\n",table[index].indirectptr,indirectptrtemp2[0]);
            if(table[index].size %BLOCK_SZ ==0){
                remain = table[index].size/BLOCK_SZ;
            }
            else{
                remain = table[index].size/BLOCK_SZ +1;
            }
            for(i=0;i<remain;i++){
              if(indirectptrtemp2[i]>0){
                write_blocks(indirectptrtemp2[i],1,emptyblock);
                rm_index(indirectptrtemp2[i]);
                updatefreebitmap();
              }
	        //printf("i:%d\n",i);
            }
            write_blocks(table[index].indirectptr,1,emptyblock);
            //printf("final: %d\n",table[index].indirectptr);
            rm_index(table[index].indirectptr);

            updatefreebitmap();
            table[index].indirectptr = NUM_BLOCKS +1;
        }

    }
	
	return 0;
}
