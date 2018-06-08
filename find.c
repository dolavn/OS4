#include "types.h"
#include "user.h"

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

void get_search_criteria(int, char**);

void search(struct search_criteria*);

int main(int argc, char** argv){

  if(argc < 2) {
    printf(2, "Usage: find <path> [<options>] [<tests>]\n");
    exit();
  }

  
  exit();
}
