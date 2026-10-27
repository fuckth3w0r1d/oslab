#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main()
{
  char cmd[0x30] = {0};
  while(1)
  {
    memset(cmd, 0, 0x30);
    printf("~ (∠・ω< )⌒  $ ");
    int idx = 0;
    char ch = 0;
    while(ch != '\n')
    {
      read(0, &ch, 1);
      cmd[idx++] = ch;
      if(idx == 0x30)
      {
        printf("\ntoo longggggggggggg!!!!!!!\n");
        exit(1);
      }
    }
    cmd[idx] = 0;
    printf("You want to exec: %s", cmd);
    printf("no no no this is a fake shell.\n");
    printf("You are in trappp!!!\n");
  }
  return 0;
}