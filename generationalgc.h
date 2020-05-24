#ifndef GC_GENERATIONALGC_H
#define GC_GENERATIONALGC_H
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct Header
{
    size_t flag;
    size_t size;
    size_t age;
    struct Header *next_free;
    struct Header *forwarding;
} Header;

// allocate memory from the end of gc_heap instead of using pointer top
typedef struct gc_heap
{
    Header *start;
    size_t size;
} GCHeap;

struct root_range{
    void *start;
    void *end;
};

// flags
#define FL_MARK 0x1
#define FL_ALLOC 0x2
#define FL_REMEMBER 0X4
#define FL_SET(x, f) (((Header*)x)->flag |= f)
#define FL_CLEAR(x, f) ((Header*)x)->flag &= f
#define FL_ALLOC_SET(x) FL_SET(x, FL_ALLOC)
#define FL_ALLOC_CLEAR(x) FL_CLEAR(x, ~FL_ALLOC)
#define FL_MARK_SET(x) FL_SET(x, FL_MARK)
#define FL_MARK_CLEAR(x) FL_CLEAR(x, ~FL_MARK)
#define FL_REMEMBER_SET(x) FL_SET(x, FL_REMEMBER)
#define FL_REMEMBER_CLEAR(x) FL_CLEAR(x, ~FL_REMEMBER)
#define FL_TEST(x, f) (((Header *)x)->flag & f)

//size
#define DEFAULT_YOUNG_GEN_HEAP_SIZE 1000
#define DEFAULT_OLD_GEN_HEAP_SIZE 2200

#define PTR_SIZE ((size_t)sizeof(void*))
#define HEADER_SIZE ((size_t)sizeof(Header))
#define ALIGN(x, a) ((x + a - 1) & (~(a - 1)))

#define RS_LIMIT 256

//other
#define NEXT_HEADER(x) ((Header *)((size_t)(x+1) + x->size))
#define PROMOTE_AGE 2

#define STACK_BASE(b) \
    asm ( "movq %%rbp, %0"\
        :"=r"(b)          \
        :                 \
        :                 \
    );

#define STACK_TOP(b) \
    asm ( "movq %%rsp, %0"\
        :"=r"(b)          \
        :                 \
        :                 \
    );

static void* ge_malloc(size_t req_size);
void garbage_collect();
void init_root(void *start, void *end);
static void init_gc_heap(size_t young_gen_size, size_t old_gen_size);

#endif //GC_GENERATIONALGC_H
