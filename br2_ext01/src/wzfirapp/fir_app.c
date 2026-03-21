#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <assert.h>
#include "fir_ioctl.h"

int16_t coeffs[] = {3,64,128,64};
int16_t samples[] = {9,13,-12,1,2,3,5,7,8,20};
int main(int argc, char * argv[])
{
    int res;
    int delay;
    int f=open(argv[1],O_RDWR);
    delay=atoi(argv[2]);
    assert(f>0);
    res = ioctl(f,FIR_IO_DEL,(void *) delay);
    res = ioctl(f,FIR_IO_COEFFS,(void *) coeffs);
    if(res<0) perror("coeffs res");
    printf("coeffs res=%d\n",res);
    assert(res>=0);
    for(int i=0; i<2; i++) {
        res = ioctl(f,FIR_IO_SAMPLES,(void *) samples);
        if(res<0) perror("coeffs smp");
        printf("coeffs smp=%d\n",res);
        assert(res>=0);
        for(int j=0; j<samples[0]; j++) {
            printf("%d,",samples[j+1]);
        }
        printf("\n");
    }
}
