#include "sys.h"
static void say(char *s) { unsigned n=0; while(s[n]) n++; syscall(SYS_write,1,n,s); }
static void stop(void) { say("LAB9_FAIL\n"); syscall(SYS_exit,1); while(1); }
static void run(char *path,char **args)
{
    long pid=syscall(SYS_fork);
    if (pid<0) stop();
    if (!pid) {
        if (path[1]=='e' && path[2]=='l') {
            unsigned long address=syscall(SYS_mmap,0,4096);
            if (!address) stop();
            *(volatile unsigned long *)address=0x1234;
        }
        syscall(SYS_exec,path,args); stop();
    }
    int status=-1;
    if (syscall(SYS_wait,&status)!=pid || status) stop();
}
__attribute__((section(".text.entry"))) int main(void)
{
    char *args[]={"test","111","222","333",0};
    char *paths[]={"/test_1","/test_2","/test_3","/test_4"};
    char *markers[]={"COURSE_TEST_1_PASS\n","COURSE_TEST_2_PASS\n","COURSE_TEST_3_PASS\n","COURSE_TEST_4_PASS\n"};
    for(int i=0;i<4;i++) { run(paths[i],args); say(markers[i]); }
    char *edge[]={"edge",0}; run("/edge",edge);
    char *elf[]={"elf","alpha","beta",0}; run("/elfprobe",elf);
    char *fs[]={"fsprobe",0}; run("/fsprobe",fs);
    say("LAB9_PASS\n"); syscall(SYS_exit,0); return 0;
}
