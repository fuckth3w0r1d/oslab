#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// 从完整路径 path 中取出最后的文件名部分
char* fmtname(char *path)
{
char *p;
    // 找到最后一个 '/' 之后的位置
    for(p = path + strlen(path); p >= path && *p != '/'; p--);
    p++;
    return p;
}

// 递归查找：在 path 目录下查找名字为 name 的文件
// exec_cmd 不为 0 时，找到后执行 "cmd filepath"
void find(char *path, char *name, char **exec_argv, int exec_argc, int exec_cmd)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;
    if((fd = open(path, 0)) < 0)
    {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }
    if(fstat(fd, &st) < 0)
    {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }
    switch(st.type)
    {
    case T_DEVICE:
    case T_FILE:
        // 基本情况：path 本身是文件，看看是否匹配
        if(strcmp(fmtname(path), name) == 0)
        {
            if(exec_cmd)
            {
                // fork 子进程来执行命令
                int pid = fork();
                if(pid < 0)
                {
                    fprintf(2, "find: fork failed\n");
                    break;
                }
                if(pid == 0)
                {
                    // 子进程：argv = [cmd, path, 0]
                    char *argv[MAXARG];
                    for(int i = 0; i < exec_argc; i++)
                    {
                        argv[i] = exec_argv[i];
                    }
                    argv[exec_argc] = path;
                    argv[exec_argc + 1] = 0;
                    // 执行 cmd file
                    exec(argv[0], argv);
                    fprintf(2, "find: exec %s failed\n", argv[0]);
                    exit(1);
                }
                else
                {
                    wait(0);
                }
            }
            else
            {
                printf("%s\n", path);
            }
        }
        break;
    case T_DIR:
        // 处理目录：先检查目录本身是否匹配
        if(strcmp(fmtname(path), name) == 0)
        {
            if(exec_cmd)
            {
                int pid = fork();
                if(pid < 0)
                {
                    fprintf(2, "find: fork failed\n");
                    break;
                }
                if(pid == 0)
                {
                    char *argv[MAXARG];
                    for(int i = 0; i < exec_argc; i++)
                    {
                        argv[i] = exec_argv[i];
                    }
                    argv[exec_argc] = path;
                    argv[exec_argc + 1] = 0;
                    // 执行 cmd file
                    exec(argv[0], argv);
                    fprintf(2, "find: exec %s failed\n", argv[0]);
                    exit(1);
                }
                else
                {
                    wait(0);
                }
            }
            else
            {
                printf("%s\n", path);
            }
        }
        // 构造子路径前缀
        if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf)
        {
            printf("find: path too long\n");
            break;
        }
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';
        // 遍历目录项
        while(read(fd, &de, sizeof(de)) == sizeof(de))
        {
            if(de.inum == 0)
                continue;
            if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                continue;
            if(de.name[0] == 0)
                continue;
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            // 递归进入
            find(buf, name, exec_argv, exec_argc, exec_cmd);
        }
        break;
    }
    close(fd);
}

int main(int argc, char *argv[])
{
    if(argc < 3)
    {
        fprintf(2, "usage: find <dir> <name> [-exec <cmd>]\n");
        exit(1);
    }
    char *path = argv[1];
    char *name = argv[2];
    char *exec_argv[MAXARG];
    int exec_argc = 0;
    int exec_cmd = 0;
    // 重新构造执行 cmd 需要的 exec_argv, 也就是 -exec 之后的参数
    if(argc >= 4)
    {
        if(strcmp(argv[3], "-exec") == 0)
        {
            exec_cmd = 1;
            for(int j = 4; j < argc; j++)
            {
                // exec_argv 为 [cmd] + [cmd前缀参数...]
                // 执行 cmd 时需要构造 [cmd] + [cmd参数...] + [文件路径] + [NULL], 则 exec_argc + 2 应小于 MAXARG
                if(exec_argc >= MAXARG - 2)
                {
                    fprintf(2, "find: too many -exec arguments\n");
                    exit(1);
                }
                exec_argv[exec_argc++] = argv[j];
            }
        }
        else{
            fprintf(2, "usage: find <dir> <name> [-exec <cmd>]\n");
            exit(1);
        }
        if(exec_argc == 0)
        {
            fprintf(2, "find: -exec needs a command\n");
            exit(1);
        }
    }
    find(path, name, exec_argv, exec_argc, exec_cmd);
    exit(0);
}