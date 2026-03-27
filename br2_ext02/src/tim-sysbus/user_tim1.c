// SWIS 2025 Ex 3, demo code by WZab
// Sources of the data generator
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "wzab_tim1.h"

struct sched_param sp;

int main(int argc, char *argv[])
{
    int i;
    int fd;
    char line[200];
    if (argc < 2) {
        fprintf(stderr, "Usage: %s interrupt_period in ns\n", argv[0]);
        exit(EXIT_FAILURE);
    } 
    uint64_t period=atoi(argv[1]);
    uint64_t res;
    int res2;
    pthread_t tcur=pthread_self();
    sp.sched_priority = sched_get_priority_max(SCHED_RR);
    res2=pthread_setschedparam(tcur,SCHED_RR,&sp);
    printf("akuku\n");
    if(res2<0) {
       perror("set sched");
       exit(1);
     };
    printf("prior: %d sched:%d\n",sp.sched_priority,res2);
    fd=open("/dev/my_tim0",O_RDWR);
    assert(write(fd,&period,8)==8);
    for(i=0;i<1000;i++) {
       read(fd,&res,8);
       printf("%d, %ld\n",i,res);
    }
    period=0;
    assert(write(fd,&period,8)==8);
    close(fd);
}

