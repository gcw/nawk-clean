#ifndef JIT_H
#define JIT_H

#include <stddef.h>

// Function to allocate executable memory
void *jit_alloc_executable_memory(size_t size);

// Function to free executable memory
void jit_free_executable_memory(void *mem, size_t size);

#endif // JIT_H
