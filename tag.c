#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"

#define TAG 1
#define READ_TAGS 2
#define UNTAG -1

int main(int argc, char** argv){
  int option = 0;
  int fd;
  char* key;
  char* value;
  if(argc <4 || argc>5) {
    printf(2, "Usage: tag <option> <path> <key> [<value>]\n");
    exit();
  }
  if(strcmp(argv[1],"-tag")==0){
    option = TAG;
  }
  if(strcmp(argv[1],"-untag")==0){
    option = UNTAG;
  }
  if(strcmp(argv[1],"-read")==0){
    option = READ_TAGS;
  }
  if(!option){
    printf(2,"No option supplied\n");
    exit();
  }
  if((fd = open(argv[2],O_RDONLY)) < 0){
    printf(2,"Couldn't open file %s\n",argv[2]);
    exit();
  }
  key = argv[3];
  value = argc==4?"":argv[4];
  if(option==TAG){
    if(ftag(fd, key, value)<0){
      printf(2,"Couldn't add tag to file %s\n",argv[2]);
      exit();
    }
  }
  if(option==UNTAG){
    if(funtag(fd,key)<0){
      printf(2,"Couldn't remove tag from file %s\n",argv[2]);
      exit();
    }
  }
  if(option==READ_TAGS){
    printtags(fd);
  }
  close(fd);
  exit();
}
