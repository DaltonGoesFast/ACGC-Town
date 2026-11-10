/* Below-4GB heap for the arm64 game. JKR and the rest of the decomp store host pointers in u32.
 * Display-list symbols that live in the high .so go through the GBI token table instead. */
#include "pc_lowheap.h"

#include "pc_crash.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

void* __real_realloc(void* p, size_t n);
void __real_free(void* p);

#define PC_LOWHEAP_BASE ((uintptr_t)0x10000000u)
#define PC_LOWHEAP_SIZE (256u * 1024u * 1024u)

typedef struct Block {
    size_t size; /* bytes, including this header. Low bit is 1 when the block is free. */
    struct Block* next;
} Block;

static char* s_base;
static size_t s_span;
static Block* s_free;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static char s_error[512];
static int s_tried;

static size_t block_bytes(const Block* b) { return b->size & ~(size_t)1; }
static int block_is_free(const Block* b) { return (b->size & 1u) != 0; }

static size_t page_size(void) {
    long page = sysconf(_SC_PAGESIZE);
    if (page < 4096) page = 4096;
    return (size_t)page;
}

static size_t align_up(size_t n, size_t page) {
    return (n + page - 1) & ~(page - 1);
}

static void lowheap_init(void) {
    static const uintptr_t bases[] = {
        0x10000000u, 0x20000000u, 0x40000000u, 0x60000000u, 0x80000000u, 0xA0000000u, 0xC0000000u
    };
    static const unsigned divs[] = { 1u, 2u, 4u, 8u };
    size_t page = page_size();
    unsigned d, b;
    if (s_tried) return;
    s_tried = 1;
    pc_diag_init();
    for (d = 0; d < sizeof(divs) / sizeof(divs[0]); d++) {
        size_t bytes = align_up(PC_LOWHEAP_SIZE / divs[d], page);
        if (bytes < 32u * 1024u * 1024u) break;
        for (b = 0; b < sizeof(bases) / sizeof(bases[0]); b++) {
            void* p;
            if ((uint64_t)bases[b] + (uint64_t)bytes > 0x100000000ull) continue;
            p = mmap((void*)bases[b], bytes, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
            if (p == MAP_FAILED || (uintptr_t)p != bases[b]) {
                if (p != MAP_FAILED) munmap(p, bytes);
                continue;
            }
            s_base = (char*)p;
            s_span = bytes;
            s_free = (Block*)s_base;
            s_free->size = bytes | 1u;
            s_free->next = NULL;
            s_error[0] = '\0';
            {
                char msg[160];
                snprintf(msg, sizeof(msg), "heap reserve ok %uMB at 0x%lx page %u",
                         (unsigned)(bytes / (1024u * 1024u)), (unsigned long)bases[b], (unsigned)page);
                pc_diag_milestone(msg);
            }
            return;
        }
    }
    snprintf(s_error, sizeof(s_error),
             "Could not reserve a heap below 4GB (page size %u). mmap at the low addresses was refused (%s). "
             "The game stores some pointers in 32 bits, so it stops here instead of crashing later.",
             (unsigned)page, errno != 0 ? strerror(errno) : "no free low range");
    pc_diag_milestone(s_error);
}

__attribute__((constructor(101))) static void lowheap_ctor(void) { lowheap_init(); }

int pc_lowheap_ok(void) {
    lowheap_init();
    return s_base != NULL;
}

const char* pc_lowheap_error(void) {
    lowheap_init();
    return s_error[0] != '\0' ? s_error : "low heap was not reserved";
}

static void* take(size_t need) {
    Block** prev = &s_free;
    Block* b;
    need = (need + 15u) & ~(size_t)15;
    if (need < sizeof(Block) * 2) need = sizeof(Block) * 2;
    for (b = s_free; b != NULL; prev = &b->next, b = b->next) {
        size_t have = block_bytes(b);
        Block* rest;
        if (!block_is_free(b) || have < need) continue;
        if (have >= need + 64) {
            rest = (Block*)((char*)b + need);
            rest->size = (have - need) | 1u;
            rest->next = b->next;
            *prev = rest;
            b->size = need;
        } else {
            *prev = b->next;
            b->size = have;
        }
        b->next = NULL;
        return (char*)b + sizeof(Block);
    }
    return NULL;
}

void* pc_lowheap_reserve(size_t bytes) {
    void* p;
    pthread_mutex_lock(&s_lock);
    lowheap_init();
    p = take(sizeof(Block) + bytes);
    pthread_mutex_unlock(&s_lock);
    if (p == NULL) {
        fprintf(stderr, "[PC] arm64 heap: reserve of %zu bytes failed\n", bytes);
    }
    return p;
}

static int in_arena(const void* p) {
    return s_base != NULL && (const char*)p >= s_base && (const char*)p < s_base + s_span;
}

void* __wrap_malloc(size_t n) {
    void* p;
    pthread_mutex_lock(&s_lock);
    lowheap_init();
    if (s_base == NULL) {
        pthread_mutex_unlock(&s_lock);
        return NULL;
    }
    p = take(sizeof(Block) + (n ? n : 1));
    pthread_mutex_unlock(&s_lock);
    return p;
}

void* __wrap_calloc(size_t n, size_t sz) {
    size_t bytes = n * sz;
    void* p;
    if (n != 0 && bytes / n != sz) return NULL;
    p = __wrap_malloc(bytes ? bytes : 1);
    if (p) memset(p, 0, bytes);
    return p;
}

void __wrap_free(void* p) {
    Block* b;
    Block** prev;
    if (p == NULL) return;
    if (!in_arena(p)) {
        __real_free(p); /* a pointer from before the wrap, or from another library */
        return;
    }
    b = (Block*)((char*)p - sizeof(Block));
    pthread_mutex_lock(&s_lock);
    b->size |= 1u;
    prev = &s_free;
    while (*prev != NULL && *prev < b) prev = &(*prev)->next;
    b->next = *prev;
    *prev = b;
    if (b->next != NULL && (char*)b + block_bytes(b) == (char*)b->next) {
        b->size = (block_bytes(b) + block_bytes(b->next)) | 1u;
        b->next = b->next->next;
    }
    if (prev != &s_free) {
        Block* left = s_free;
        while (left != NULL && left->next != b) left = left->next;
        if (left != NULL && (char*)left + block_bytes(left) == (char*)b) {
            left->size = (block_bytes(left) + block_bytes(b)) | 1u;
            left->next = b->next;
        }
    }
    pthread_mutex_unlock(&s_lock);
}

void* __wrap_realloc(void* p, size_t n) {
    Block* b;
    void* fresh;
    size_t old;
    if (p == NULL) return __wrap_malloc(n);
    if (!in_arena(p)) return __real_realloc(p, n);
    b = (Block*)((char*)p - sizeof(Block));
    old = block_bytes(b) - sizeof(Block);
    if (old >= n) return p;
    fresh = __wrap_malloc(n);
    if (fresh == NULL) return NULL;
    memcpy(fresh, p, old < n ? old : n);
    __wrap_free(p);
    return fresh;
}
