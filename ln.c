#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  if(argc != 3 || argc != 4){
    printf(2, "Usage: ln old new OR ls -s old new\n");
    exit();
  }
  if (argv[1] != "-s") {
    if (link(argv[1], argv[2]) < 0) {
      printf(2, "link %s %s: failed\n", argv[1], argv[2]);
    }
    exit();
  }
  // Symbolic link
  
  exit();
}
