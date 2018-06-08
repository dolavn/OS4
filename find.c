#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"

struct search_criteria{
  char* path;
  char follow;
  char* name;
  int size;
  char type;
  char* key;
  char* value;
};

struct search_criteria search_crit;
int get_search_criteria(int, char**);
void search(struct search_criteria*);
int checkTests(struct search_criteria*, struct stat*, int, char*);

int main(int argc, char** argv){
  if(get_search_criteria(argc, argv) < 0) {
    printf(2, "Usage: find <path> [<options>] [<tests>]\n");
    exit();
  }
  //printf(2,"search criteria:\n%s\nfollow:%d\nname:%s\nsize:%d\ntype:%d\nkey:%s\nvalue:%s\n",search_crit.path,search_crit.follow,search_crit.name,search_crit.size,search_crit.type,search_crit.key,search_crit.value);
  exit();
}

void
search(struct search_criteria* criteria) {
  int fd;
  struct stat st;
  char* path = criteria->path;

  if((fd = open(path, criteria->follow?O_RDONLY:O_IGN_SLINK)) < 0) {
    printf(2, "find: cannot open %s\n", path);
    return;
  }
  if(fstat(fd, &st) < 0) {
    printf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type) {
  case T_FILE:
    if(checkTests(criteria, &st)) {
      printf(1, "%s\n", path);
    }
    break;

  // case T_DIR:
  //   if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
  //     printf(1, "ls: path too long\n");
  //     break;
  //   }
  //   strcpy(buf, path);
  //   p = buf+strlen(buf);
  //   *p++ = '/';
  //   while(read(fd, &de, sizeof(de)) == sizeof(de)){
  //     if(de.inum == 0)
  //       continue;
  //     memmove(p, de.name, DIRSIZ);
  //     p[DIRSIZ] = 0;
  //     if(stat(buf, &st) < 0){
  //       printf(1, "ls: cannot stat %s\n", buf);
  //       continue;
  //     }
  //     printf(1, "%s %d %d %d\n", fmtname(buf), st.type, st.ino, st.size);
  //   }
  //   break;
  }

  close(fd);
}


int checkTests(struct search_criteria* sc, struct stat* st, int fd, char* name) {
  int nameTest, typeTest, sizeTest, tagTest;
  char tval[TAGVAL_MAX_LEN];

  nameTest = ((sc->name && sc->name == name) || !sc->name);
  typeTest = ((sc->type &&  ((sc->type == 'd' && st->type == T_DIR) ||
                             (sc->type == 'f' && st->type == T_FILE) ||
                             (sc->type == 's' && st->type == T_SLINK))) ||
                             !st->type);
  if (sc->key) {
    if (gettag(fd, sc->key, tval) < 0) {
     tagTest = 0;
    }
    else if (strcmp(sc->value, '?') || strcmp(sc->value, tval)) tagTest = 1;
    else tagTest = 0;
  }

  return nameTest && typeTest && sizeTest && tagTest;
}

int get_search_criteria(int argc, char** argv){
  if(argc<2){return -1;}
  search_crit.path = argv[1];
  search_crit.follow = 0;
  search_crit.name = 0;
  search_crit.size = 0;
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
      if(argv[i+1][0]!='+' && argv[i+1][0]!='-'){
        return -1;
      }
      search_crit.size = (argv[i+1][0]=='+'?1:-1)*(atoi(argv[i+1]+1));
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
