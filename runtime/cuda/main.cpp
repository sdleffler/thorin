#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "gpu_runtime.h"

static int num = 1024;

int main() {
    int *host = (int *)malloc(num*sizeof(int));
    
    for (unsigned int i=0; i<num; ++i) {
        host[i] = 0;
    }

    // CODE TO BE GENERATED: BEGIN
    CUdeviceptr dev;
    dev = malloc_gpu(num*sizeof(int));
    mem_to_gpu(host, dev, num*sizeof(int));

    load_kernel("simple-gpu64.nvvm", "simple");
    set_kernel_arg(&dev);
    set_problem_size(1024, 1, 1);
    launch_kernel("simple");
    synchronize(); // optional
    mem_to_host(dev, host, num*sizeof(int));
    free_gpu(dev);
    // CODE TO BE GENERATED: END

    // check result
    for (unsigned int i=0; i<num; ++i) {
        if (host[i] != i) {
            printf("Test failed!\n");
            return EXIT_FAILURE;
        }
    }
    printf("Test passed!\n");

    return EXIT_SUCCESS;
}
