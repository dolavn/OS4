#include "types.h"
#include "stat.h"
#include "user.h"
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
int checkTests(struct search_criteria*, struct stat*);

int main(int argc, char** argv){

  if(argc < 2) {
    printf(2, "Usage: find <path> [<options>] [<tests>]\n");
    exit();
  }


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

int checkTests(struct search_criteria* sc, struct stat* st) {
  /* code */
  return 0;
}
