#ifndef JIT_H
#define JIT_H

#include <stddef.h>
#include <stdint.h>
#include "awk.h" // Include awk.h for Node and Cell structures

typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t offset;
} JitContext;

typedef struct {
    Cell *loop_var_cell; // Example: pointer to the Cell of the loop variable
} JitRuntimeContext;

// Initialize a JitContext
bool jit_init_context(JitContext *ctx, size_t capacity);

// Emit a single byte
bool jit_emit_byte(JitContext *ctx, uint8_t byte);

// Emit a 32-bit integer
bool jit_emit_int32(JitContext *ctx, int32_t value);

// Emit a 64-bit integer
bool jit_emit_int64(JitContext *ctx, int64_t value);

// Patch a 32-bit integer at a specific offset
bool jit_patch_int32(JitContext *ctx, size_t offset, int32_t value);

// Emit a NOP instruction (for alignment or placeholders)
bool jit_emit_nop(JitContext *ctx);


// Function to allocate executable memory
void *jit_alloc_executable_memory(size_t size);

// Function to free executable memory
void jit_free_executable_memory(void *mem, size_t size);

// Compile an AST node into machine code
bool jit_compile_node(JitContext *ctx, Node *node, JitRuntimeContext *runtime_ctx);

// Helper function for string concatenation (called from JIT-compiled code)
char *jit_cat_helper(char *s1, char *s2);

// Helper functions for JIT-compiled code to interact with Cell values
double jit_get_fval(Cell *c);
char *jit_getsval(Cell *c);
void jit_set_fval(Cell *c, double val);
void jit_setsval(Cell *c, char *s);

// Standard library functions for JIT calls
int printf(const char *format, ...);

#endif // JIT_H
