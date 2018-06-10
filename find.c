#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"

#define EQUAL 0
#define BIGGER_THAN 1
#define SMALLER_THAN -1
#define NO_SIZE_TYPE 2
#define IS_DIGIT(a)     (a>='0' && a<='9')

struct search_criteria{
  char* path;
  char follow;
  char* name;
  int size;
  char size_type;
  char type;
  char* key;
  char* value;
};

struct search_criteria search_crit;
int get_search_criteria(int, char**);
void search(struct search_criteria*);
void rec_search(struct search_criteria*,char*,char*);
int checkTests(struct search_criteria*, struct stat*, int, char*);

int main(int argc, char** argv){
  if(get_search_criteria(argc, argv) < 0) {
    printf(2, "Usage: find <path> [<options>] [<tests>]\n");
    exit();
  }
  search(&search_crit);
  //printf(2,"search criteria:\n%s\nfollow:%d\nname:%s\nsize:%d\nsize_type:%d\ntype:%d\nkey:%s\nvalue:%s\n",search_crit.path,search_crit.follow,search_crit.name,search_crit.size,search_crit.size_type,search_crit.type,search_crit.key,search_crit.value);
  exit();
}

void
rec_search(struct search_criteria* criteria, char* path, char* name){
  int fd;
  char* p = path+strlen(path);
  char* new_path = 0;
  char** dir; char** curr;
  struct stat st;
  struct dirent de;
  if((fd = open(path, O_IGN_SLINK)) < 0) {
    printf(2, "find: cannot open %s\n", path);
    return;
  }
  if(fstat(fd, &st) < 0) {
    printf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }
  printf(2,"hey\n");
  if(criteria->follow && st.type==T_SLINK){
    new_path = (char*)(malloc(sizeof(char)*512));
    readlink(path,new_path,512);
    path = new_path;
    p = path+strlen(path);
    close(fd);
    fd = open(path,O_IGN_SLINK);
    if(fstat(fd, &st) < 0){
      printf(2, "find: cannot stat %s\n", path);
      close(fd);
      return;
    }
  }
  if(checkTests(criteria, &st, fd, name)) {
    printf(2, "%s\n", path);
  }
  if(st.type==T_DIR){
    if(*(p-1)!='/'){
      *p++ = '/';
    }
    dir = (char**)(malloc(sizeof(char*)*2*((st.size/sizeof(de))+1)));
    curr = dir;
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      *curr = (char*)(malloc(sizeof(char)*(strlen(path)+1)));
      printf(2,"malloc %d\n",curr-dir);
      memmove(*curr,path,strlen(path)+1);
      curr++;
      *curr = *(curr-1)+(p-path);
      printf(2,"%s\n",*curr);
      curr++;
      p[DIRSIZ] = 0;
    }
    close(fd);
    for(curr=dir;*curr;curr=curr+2){
      printf(2,"curr:%d\n",curr-dir);
      //printf(2,"p:%p\ncurr:%s\n",*curr,*curr);
      if(strcmp(*(curr+1),".")==0 || strcmp(*(curr+1),"..")==0){
        continue;
      }
      rec_search(criteria, *curr, *(curr+1));
      free(*curr);
      *curr = 0;
      *(curr+1) = 0;
    }
    free(dir);
    dir = 0;
  }
  
  if(new_path){
    free(new_path);
    path = 0;
    p=0;
    new_path = 0;
  }
  close(fd);
}

int
checkTests(struct search_criteria* sc, struct stat* st, int fd, char* name) {
  int nameTest = 1, typeTest = 1, sizeTest = 1, tagTest = 1;
  char tval[TAGVAL_MAX_LEN];

  nameTest = ((sc->name && strcmp(sc->name, name)==0) || !sc->name);
  typeTest = ((sc->type && (sc->type == st->type))    || !sc->type);
  sizeTest = ((sc->size && ((sc->size_type == EQUAL && sc->size == st->size) ||
                            (sc->size_type == BIGGER_THAN && sc->size <= st->size) ||
                            (sc->size_type == SMALLER_THAN && sc->size >= st->size))) ||
                           !sc->size);
  if (sc->key) {
    if (gettag(fd, sc->key, tval) < 0) {
      tagTest = 0;
    }
    else if (strcmp(sc->value, "?")==0 || strcmp(sc->value, tval)==0) tagTest = 1;
    else tagTest = 0;
  }
  else tagTest = 1;

  // printf(2, "cname:\tt c:t\ts c:t\n%s\t%d:%d\t%d:%d\t\n", sc->name, sc->type, st->type, sc->size, st->size);
  // printf(2, "%d\t%d\t%d\t%d\t\n", nameTest, typeTest, sizeTest, tagTest);
  return nameTest && typeTest && sizeTest && tagTest;
}


void
search(struct search_criteria* criteria) {
  char path[512];
  memmove(path,criteria->path,strlen(criteria->path)+1);
  rec_search(criteria,path,path);
}

int
get_search_criteria(int argc, char** argv){
  if(argc<2){return -1;}
  search_crit.path = argv[1];
  search_crit.follow = 0;
  search_crit.name = 0;
  search_crit.size = 0;
  search_crit.size_type = NO_SIZE_TYPE;
  search_crit.type = 0;
  search_crit.key = 0;
  search_crit.value = 0;
  for(int i=2;i<argc;++i){
    if(strcmp(argv[i],"-follow")==0){
      search_crit.follow=1;
      continue;
    }
    if(strcmp(argv[i],"-name")==0){
      if(i==argc-1){return -1;}
      search_crit.name = argv[i+1];
      i = i + 1;
      continue;
    }
    if(strcmp(argv[i],"-size")==0){
      if(i==argc-1){return -1;}
      char* num = argv[i+1];
      if(argv[i+1][0]=='+'){
        num++;
        search_crit.size_type = BIGGER_THAN;
      }
      if(argv[i+1][0]=='-'){
        num++;
        search_crit.size_type = SMALLER_THAN;
      }
      if(IS_DIGIT(argv[i+1][0])){
        search_crit.size_type = EQUAL;
      }
      if(search_crit.size_type == NO_SIZE_TYPE){
        return -1;
      }
      for(char* digit=num;*digit;digit++){
        if(!IS_DIGIT(*digit)){return -1;}
      }
      search_crit.size = atoi(num);
      i = i + 1;
      continue;
    }
    if(strcmp(argv[i],"-type")==0){
      if(i==argc-1){return -1;}
      if(strcmp(argv[i+1],"d")==0){
        search_crit.type = T_DIR;
        i = i + 1;
        continue;
      }
      if(strcmp(argv[i+1],"f")==0){
        search_crit.type = T_FILE;
        i = i + 1;
        continue;
      }
      if(strcmp(argv[i+1],"s")==0){
        search_crit.type = T_SLINK;
        i = i + 1;
        continue;
      }
      return -1;
    }
    if(strcmp(argv[i],"-tag")==0){
      if(i==argc-1){return -1;}
      for(char* value=argv[i+1];*value;value++){
        if(*value=='='){
          *value = 0;
          search_crit.value = value+1;
          break;
        }
      }
      if(!search_crit.value){return -1;}
      search_crit.key = argv[i+1];
    }
  }
  return 0;
}
