#include "jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>

// Function to allocate executable memory
void *jit_alloc_executable_memory(size_t size)
{
    void *mem = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mem == MAP_FAILED) {
        perror("mmap failed");
        return NULL;
    }
    return mem;
}

// Function to free executable memory
void jit_free_executable_memory(void *mem, size_t size)
{
    if (munmap(mem, size) == -1) {
        perror("munmap failed");
    }
}
