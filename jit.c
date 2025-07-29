#include "jit.h"
#include "awk.h" // Include awk.h for Node and Cell structures
#include "awkgram.tab.h" // Include for token definitions like NUMBER
#include <sys/mman.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Helper function for string concatenation (called from JIT-compiled code)
char *jit_cat_helper(char *s1, char *s2)
{
    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);
    char *result = (char *)malloc(len1 + len2 + 1);
    if (result == NULL) {
        perror("malloc failed in jit_cat_helper");
        exit(EXIT_FAILURE);
    }
    memcpy(result + len1, s2, len2);
    result[len1 + len2] = '\0';
    return result;
}

// Helper functions for JIT-compiled code to interact with Cell values
double jit_get_fval(Cell *c) {
    return c->fval;
}

char *jit_getsval(Cell *c) {
    return getsval(c);
}

void jit_set_fval(Cell *c, double val) {
    setfval(c, val);
}

void jit_setsval(Cell *c, char *s) {
    setsval(c, s);
}

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

// Initialize a JitContext
bool jit_init_context(JitContext *ctx, size_t capacity)
{
    ctx->buffer = (uint8_t *)jit_alloc_executable_memory(capacity);
    ctx->capacity = capacity;
    ctx->offset = 0;
    if (ctx->buffer == NULL) {
        fprintf(stderr, "Failed to allocate JIT buffer\n");
        return false;
    }
    return true;
}

// Emit a single byte
bool jit_emit_byte(JitContext *ctx, uint8_t byte)
{
    if (ctx->offset >= ctx->capacity) {
        fprintf(stderr, "JIT buffer overflow\n");
        return false;
    }
    ctx->buffer[ctx->offset++] = byte;
    return true;
}

// Emit a 32-bit integer
bool jit_emit_int32(JitContext *ctx, int32_t value)
{
    if (ctx->offset + sizeof(int32_t) > ctx->capacity) {
        fprintf(stderr, "JIT buffer overflow\n");
        return false;
    }
    memcpy(ctx->buffer + ctx->offset, &value, sizeof(int32_t));
    ctx->offset += sizeof(int32_t);
    return true;
}

// Emit a 64-bit integer
bool jit_emit_int64(JitContext *ctx, int64_t value)
{
    if (ctx->offset + sizeof(int64_t) > ctx->capacity) {
        fprintf(stderr, "JIT buffer overflow\n");
        return false;
    }
    memcpy(ctx->buffer + ctx->offset, &value, sizeof(int64_t));
    ctx->offset += sizeof(int64_t);
    return true;
}

// Emit a NOP instruction (for alignment or placeholders)
bool jit_emit_nop(JitContext *ctx)
{
    return jit_emit_byte(ctx, 0x90); // x86 NOP instruction
}

// Compile an AST node into machine code
bool jit_compile_node(JitContext *ctx, Node *node, JitRuntimeContext *runtime_ctx)
{
    switch (node->nobj) {
        case NUMBER: {
            Cell *c = (Cell *)node->narg[0];
            int64_t val = (int64_t)c->fval; // Assuming fval holds the numeric value

            // mov rax, imm64 (0x48 0xB8 followed by 8-byte immediate)
            if (!jit_emit_byte(ctx, 0x48)) return false;
            if (!jit_emit_byte(ctx, 0xB8)) return false;
            if (!jit_emit_int64(ctx, *(int64_t*)&c->fval)) return false; // Load raw bits of double

            // movq xmm0, rax (move integer to XMM0)
            if (!jit_emit_byte(ctx, 0x66)) return false; // Operand size prefix
            if (!jit_emit_byte(ctx, 0x0F)) return false; // SSE opcode prefix
            if (!jit_emit_byte(ctx, 0x6E)) return false; // MOVQ
            if (!jit_emit_byte(ctx, 0xC0)) return false; // XMM0, RAX
            }
            break;
        case STRING: {
            Cell *c = (Cell *)node->narg[0];
            char *s = c->sval;

            // mov rax, imm64 (address of string literal)
            if (!jit_emit_byte(ctx, 0x48)) return false; // REX.W
            if (!jit_emit_byte(ctx, 0xB8)) return false; // MOV RAX, imm64
            if (!jit_emit_int64(ctx, (int64_t)s)) return false;
            }
            break;
        case VAR: {
            // mov rdi, runtime_ctx->loop_var_cell
            if (!jit_emit_byte(ctx, 0x48)) return false; // REX.W
            if (!jit_emit_byte(ctx, 0xBF)) return false; // MOV RDI, imm64
            if (!jit_emit_int64(ctx, (int64_t)runtime_ctx->loop_var_cell)) return false;

            // mov rax, address of jit_get_fval
            if (!jit_emit_byte(ctx, 0x48)) return false; // REX.W
            if (!jit_emit_byte(ctx, 0xB8)) return false; // MOV RAX, imm64
            if (!jit_emit_int64(ctx, (int64_t)jit_get_fval)) return false;

            // call rax
            if (!jit_emit_byte(ctx, 0xFF)) return false; // CALL
            if (!jit_emit_byte(ctx, 0xD0)) return false; // RAX

            // movq xmm0, rax (move integer to XMM0)
            if (!jit_emit_byte(ctx, 0x66)) return false; // Operand size prefix
            if (!jit_emit_byte(ctx, 0x0F)) return false; // SSE opcode prefix
            if (!jit_emit_byte(ctx, 0x6E)) return false; // MOVQ
            if (!jit_emit_byte(ctx, 0xC0)) return false; // XMM0, RAX
            }
            break;
        case ASSIGN: {
            // Compile the right-hand side expression. Result (double) will be in XMM0.
            if (!jit_compile_node(ctx, node->narg[1], runtime_ctx)) return false;

            // mov rsi, rax (move value to RSI for jit_set_fval)
            if (!jit_emit_byte(ctx, 0x48)) return false; // REX.W
            if (!jit_emit_byte(ctx, 0x89)) return false; // MOV
            if (!jit_emit_byte(ctx, 0xC6)) return false; // RSI, RAX

            // mov rdi, runtime_ctx->loop_var_cell
            if (!jit_emit_byte(ctx, 0x48)) return false; // REX.W
            if (!jit_emit_byte(ctx, 0xBF)) return false; // MOV RDI, imm64
            if (!jit_emit_int64(ctx, (int64_t)runtime_ctx->loop_var_cell)) return false;

            // mov rax, address of jit_set_fval
            if (!jit_emit_byte(ctx, 0x48)) return false; // REX.W
            if (!jit_emit_byte(ctx, 0xB8)) return false; // MOV RAX, imm64
            if (!jit_emit_int64(ctx, (int64_t)jit_set_fval)) return false;

            // call rax
            if (!jit_emit_byte(ctx, 0xFF)) return false; // CALL
            if (!jit_emit_byte(ctx, 0xD0)) return false; // RAX
            }
            break;
        case MULT: {
            // Compile left operand (result in XMM0)
            if (!jit_compile_node(ctx, node->narg[0], runtime_ctx)) return false;
            // Push XMM0 onto stack
            // sub rsp, 8
            if (!jit_emit_byte(ctx, 0x48)) return false; // REX.W
            if (!jit_emit_byte(ctx, 0x83)) return false; // SUB
            if (!jit_emit_byte(ctx, 0xEC)) return false; // RSP, imm8
            if (!jit_emit_byte(ctx, 0x08)) return false; // 8

            // movsd [rsp], xmm0
            if (!jit_emit_byte(ctx, 0xF2)) return false; // REP prefix for MOVSD
            if (!jit_emit_byte(ctx, 0x0F)) return false; // MOVSD opcode prefix
            if (!jit_emit_byte(ctx, 0x11)) return false; // MOVSD opcode
            if (!jit_emit_byte(ctx, 0x04)) return false; // [RSP]
            if (!jit_emit_byte(ctx, 0x24)) return false; // SIB byte

            // Compile right operand (result in XMM0)
            if (!jit_compile_node(ctx, node->narg[1], runtime_ctx)) return false;

            // Pop previous result into XMM1
            // movsd xmm1, [rsp]
            if (!jit_emit_byte(ctx, 0xF2)) return false; // REP prefix for MOVSD
            if (!jit_emit_byte(ctx, 0x0F)) return false; // MOVSD opcode prefix
            if (!jit_emit_byte(ctx, 0x10)) return false; // MOVSD opcode
            if (!jit_emit_byte(ctx, 0x0C)) return false; // XMM1, [RSP]
            if (!jit_emit_byte(ctx, 0x24)) return false; // SIB byte

            // add rsp, 8
            if (!jit_emit_byte(ctx, 0x48)) return false; // REX.W
            if (!jit_emit_byte(ctx, 0x83)) return false; // ADD
            if (!jit_emit_byte(ctx, 0xC4)) return false; // RSP, imm8
            if (!jit_emit_byte(ctx, 0x08)) return false; // 8

            // mulsd xmm0, xmm1 (XMM0 = XMM0 * XMM1)
            if (!jit_emit_byte(ctx, 0xF2)) return false; // REP prefix for MULSD
            if (!jit_emit_byte(ctx, 0x0F)) return false; // MULSD opcode prefix
            if (!jit_emit_byte(ctx, 0x59)) return false; // MULSD opcode
            if (!jit_emit_byte(ctx, 0xC1)) return false; // XMM0, XMM1
            }
            break;
        case ADD: {
            // Compile left operand (result in XMM0)
            if (!jit_compile_node(ctx, node->narg[0], runtime_ctx)) return false;
            // Push XMM0 onto stack
            // sub rsp, 8
            if (!jit_emit_byte(ctx, 0x48)) return false; // REX.W
            if (!jit_emit_byte(ctx, 0x83)) return false; // SUB
            if (!jit_emit_byte(ctx, 0xEC)) return false; // RSP, imm8
            if (!jit_emit_byte(ctx, 0x08)) return false; // 8

            // movsd [rsp], xmm0
            if (!jit_emit_byte(ctx, 0xF2)) return false; // REP prefix for MOVSD
            if (!jit_emit_byte(ctx, 0x0F)) return false; // MOVSD opcode prefix
            if (!jit_emit_byte(ctx, 0x11)) return false; // MOVSD opcode
            if (!jit_emit_byte(ctx, 0x04)) return false; // [RSP]
            if (!jit_emit_byte(ctx, 0x24)) return false; // SIB byte

            // Compile right operand (result in XMM0)
            if (!jit_compile_node(ctx, node->narg[1], runtime_ctx)) return false;

            // Pop previous result into XMM1
            // movsd xmm1, [rsp]
            if (!jit_emit_byte(ctx, 0xF2)) return false; // REP prefix for MOVSD
            if (!jit_emit_byte(ctx, 0x0F)) return false; // MOVSD opcode prefix
            if (!jit_emit_byte(ctx, 0x10)) return false; // MOVSD opcode
            if (!jit_emit_byte(ctx, 0x0C)) return false; // XMM1, [RSP]
            if (!jit_emit_byte(ctx, 0x24)) return false; // SIB byte

            // add rsp, 8
            if (!jit_emit_byte(ctx, 0x48)) return false; // REX.W
            if (!jit_emit_byte(ctx, 0x83)) return false; // ADD
            if (!jit_emit_byte(ctx, 0xC4)) return false; // RSP, imm8
            if (!jit_emit_byte(ctx, 0x08)) return false; // 8

            // addsd xmm0, xmm1 (XMM0 = XMM0 + XMM1)
            if (!jit_emit_byte(ctx, 0xF2)) return false; // REP prefix for ADDSD
            if (!jit_emit_byte(ctx, 0x0F)) return false; // ADDSD opcode prefix
            if (!jit_emit_byte(ctx, 0x58)) return false; // ADDSD opcode
            if (!jit_emit_byte(ctx, 0xC1)) return false; // XMM0, XMM1
            }
            break;
        case DIVIDE: {
            // Compile left operand (result in XMM0)
            if (!jit_compile_node(ctx, node->narg[0], runtime_ctx)) return false;
            // Push XMM0 onto stack
            // sub rsp, 8
            if (!jit_emit_byte(ctx, 0x48)) return false; // REX.W
            if (!jit_emit_byte(ctx, 0x83)) return false; // SUB
            if (!jit_emit_byte(ctx, 0xEC)) return false; // RSP, imm8
            if (!jit_emit_byte(ctx, 0x08)) return false; // 8

            // movsd [rsp], xmm0
            if (!jit_emit_byte(ctx, 0xF2)) return false; // REP prefix for MOVSD
            if (!jit_emit_byte(ctx, 0x0F)) return false; // MOVSD opcode prefix
            if (!jit_emit_byte(ctx, 0x11)) return false; // MOVSD opcode
            if (!jit_emit_byte(ctx, 0x04)) return false; // [RSP]
            if (!jit_emit_byte(ctx, 0x24)) return false; // SIB byte

            // Compile right operand (result in XMM0)
            if (!jit_compile_node(ctx, node->narg[1], runtime_ctx)) return false;

            // Pop previous result into XMM1
            // movsd xmm1, [rsp]
            if (!jit_emit_byte(ctx, 0xF2)) return false; // REP prefix for MOVSD
            if (!jit_emit_byte(ctx, 0x0F)) return false; // MOVSD opcode prefix
            if (!jit_emit_byte(ctx, 0x10)) return false; // MOVSD opcode
            if (!jit_emit_byte(ctx, 0x0C)) return false; // XMM1, [RSP]
            if (!jit_emit_byte(ctx, 0x24)) return false; // SIB byte

            // add rsp, 8
            if (!jit_emit_byte(ctx, 0x48)) return false; // REX.W
            if (!jit_emit_byte(ctx, 0x83)) return false; // ADD
            if (!jit_emit_byte(ctx, 0xC4)) return false; // RSP, imm8
            if (!jit_emit_byte(ctx, 0x08)) return false; // 8

            // divsd xmm0, xmm1 (XMM0 = XMM0 / XMM1)
            if (!jit_emit_byte(ctx, 0xF2)) return false; // REP prefix for DIVSD
            if (!jit_emit_byte(ctx, 0x0F)) return false; // DIVSD opcode prefix
            if (!jit_emit_byte(ctx, 0x5E)) return false; // DIVSD opcode
            if (!jit_emit_byte(ctx, 0xC1)) return false; // XMM0, XMM1
            }
            break;
        case MOD: {
            // Compile left operand (result in XMM0)
            if (!jit_compile_node(ctx, node->narg[0], runtime_ctx)) return false;
            // Push XMM0 onto stack
            // sub rsp, 8
            if (!jit_emit_byte(ctx, 0x48)) return false; // REX.W
            if (!jit_emit_byte(ctx, 0x83)) return false; // SUB
            if (!jit_emit_byte(ctx, 0xEC)) return false; // RSP, imm8
            if (!jit_emit_byte(ctx, 0x08)) return false; // 8

            // movsd [rsp], xmm0
            if (!jit_emit_byte(ctx, 0xF2)) return false; // REP prefix for MOVSD
            if (!jit_emit_byte(ctx, 0x0F)) return false; // MOVSD opcode prefix
            if (!jit_emit_byte(ctx, 0x11)) return false; // MOVSD opcode
            if (!jit_emit_byte(ctx, 0x04)) return false; // [RSP]
            if (!jit_emit_byte(ctx, 0x24)) return false; // SIB byte

            // Compile right operand (result in XMM0)
            if (!jit_compile_node(ctx, node->narg[1], runtime_ctx)) return false;

            // Pop previous result into XMM1
            // movsd xmm1, [rsp]
            if (!jit_emit_byte(ctx, 0xF2)) return false; // REP prefix for MOVSD
            if (!jit_emit_byte(ctx, 0x0F)) return false; // MOVSD opcode prefix
            if (!jit_emit_byte(ctx, 0x10)) return false; // MOVSD opcode
            if (!jit_emit_byte(ctx, 0x0C)) return false; // XMM1, [RSP]
            if (!jit_emit_byte(ctx, 0x24)) return false; // SIB byte

            // add rsp, 8
            if (!jit_emit_byte(ctx, 0x48)) return false; // REX.W
            if (!jit_emit_byte(ctx, 0x83)) return false; // ADD
            if (!jit_emit_byte(ctx, 0xC4)) return false; // RSP, imm8
            if (!jit_emit_byte(ctx, 0x08)) return false; // 8

            // Call fmod(XMM0, XMM1)
            // mov rdi, xmm0 (double in XMM0 to RDI)
            if (!jit_emit_byte(ctx, 0x48)) return false; // REX.W
            if (!jit_emit_byte(ctx, 0x89)) return false; // MOV
            if (!jit_emit_byte(ctx, 0xC7)) return false; // RDI, RAX

            // mov rsi, xmm1 (double in XMM1 to RSI)
            if (!jit_emit_byte(ctx, 0x48)) return false; // REX.W
            if (!jit_emit_byte(ctx, 0x89)) return false; // MOV
            if (!jit_emit_byte(ctx, 0xCE)) return false; // RSI, RCX

            // mov rax, address of fmod
            if (!jit_emit_byte(ctx, 0x48)) return false; // REX.W
            if (!jit_emit_byte(ctx, 0xB8)) return false; // MOV RAX, imm64
            if (!jit_emit_int64(ctx, (int64_t)fmod)) return false;

            // call rax
            if (!jit_emit_byte(ctx, 0xFF)) return false; // CALL
            if (!jit_emit_byte(ctx, 0xD0)) return false; // RAX
            }
            break;
        case UMINUS: {
            // Compile operand (result in XMM0)
            if (!jit_compile_node(ctx, node->narg[0], runtime_ctx)) return false;

            // xorpd xmm1, xmm1 (XMM1 = 0.0)
            if (!jit_emit_byte(ctx, 0x66)) return false; // Operand size prefix
            if (!jit_emit_byte(ctx, 0x0F)) return false; // SSE opcode prefix
            if (!jit_emit_byte(ctx, 0x57)) return false; // XORPD
            if (!jit_emit_byte(ctx, 0xC9)) return false; // XMM1, XMM1

            // subsd xmm0, xmm1 (XMM0 = XMM1 - XMM0) to negate
            if (!jit_emit_byte(ctx, 0xF2)) return false; // REP prefix for SUBSD
            if (!jit_emit_byte(ctx, 0x0F)) return false; // SUBSD opcode prefix
            if (!jit_emit_byte(ctx, 0x5C)) return false; // SUBSD opcode
            if (!jit_emit_byte(ctx, 0xC8)) return false; // XMM1, XMM0

            // movapd xmm0, xmm1 (move result to XMM0)
            if (!jit_emit_byte(ctx, 0x66)) return false; // Operand size prefix
            if (!jit_emit_byte(ctx, 0x0F)) return false; // SSE opcode prefix
            if (!jit_emit_byte(ctx, 0x28)) return false; // MOVAPD
            if (!jit_emit_byte(ctx, 0xC1)) return false; // XMM0, XMM1
            }
            break;
        default:
            fprintf(stderr, "JIT: Unsupported node type %d\n", node->nobj);
            return false;
    }
    return true;
}