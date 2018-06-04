#include "types.h"
#include "user.h"
#include "fcntl.h"

#define TOTAL_FILE_SIZE (1<<20)
#define BLOCK_SIZE (1<<9)
#define DIRECT_BLOCKS 12
#define INDIRECT_BLOCKS 128
#define DINDIRECT_BLOCKS (TOTAL_FILE_SIZE/BLOCK_SIZE)-DIRECT_BLOCKS-INDIRECT_BLOCKS


char buffer[BLOCK_SIZE];

int main(int argc, char** argv){
    int fd = open("testFile",O_CREATE | O_RDWR);
    symlink("a","a");
    printf(2,"Writing to direct blocks\n");
    for(int i=0;i<DIRECT_BLOCKS;++i){
        if(write(fd,buffer,BLOCK_SIZE)<0){
            exit();
        }
    }
    printf(2,"Finished writing to direct blocks\n");
    printf(2,"Writing to indirect blocks\n");
    for(int i=0;i<INDIRECT_BLOCKS;++i){
        if(write(fd,buffer,BLOCK_SIZE)<0){
            exit();
        }
    }
    printf(2,"Finished writing to indirect blocks\n");
    printf(2,"Writing to double indirect blocks\n");
    for(int i=0;i<DINDIRECT_BLOCKS;++i){
        if(write(fd,buffer,BLOCK_SIZE)<0){
            exit();
        }
    }
    printf(2,"Finished writing to double indirect blocks\n");
    exit();
}