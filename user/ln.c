#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int ret;

  if(argc == 4 && strcmp(argv[1], "-s") == 0){
    ret = symlink(argv[2], argv[3]);
  } else if(argc == 3){
    ret = link(argv[1], argv[2]);
  } else {
    fprintf(2, "usage: ln [-s] target linkpath\n");
    exit(1);
  }

  if(ret < 0){
    fprintf(2, "ln: failed\n");
    exit(1);
  }

  exit(0);
}