#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  char* oldp;
  char* newp;

  if(argc < 3 || argc > 4){
    printf(2, "Usage: ln old new OR ls -s old new\n");
    exit();
  }
  if (strcmp(argv[1],"-s")) {
    if (link(argv[1], argv[2]) < 0) {
      printf(2, "link %s %s: failed\n", argv[1], argv[2]);
    }
    exit();
  }
  // Symbolic link
  oldp = argv[2];
  newp = argv[3];
  if (symlink(oldp, newp) != 0) {
    printf(2, "symlink %s %s: failed\n", oldp, newp);
  }

  exit();
}
