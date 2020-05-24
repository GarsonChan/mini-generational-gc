#include "generationalgc.h"

static struct gc_heap young_gen;
static struct gc_heap old_gen;
static struct gc_heap from_survivor;
static struct gc_heap to_survivor;

static struct Header *free_list;
static unsigned long stack_base;
static unsigned long stack_top;

static size_t need_grow_heap = 0;


/* ========================================================================== */
/*  momery allocation                                                         */
/* ========================================================================== */
static void* ge_malloc_in_old_gen(size_t req_size);
void minor_garbage_collect();
void major_garbage_collect();


static void init_gc_heap(size_t young_gen_size, size_t old_gen_size){
    void *p, *q;
    Header *h;

    young_gen_size = young_gen_size < DEFAULT_YOUNG_GEN_HEAP_SIZE ? DEFAULT_YOUNG_GEN_HEAP_SIZE : young_gen_size;
    //allocate young gen
    if ((p = sbrk(young_gen_size + PTR_SIZE + 2 * HEADER_SIZE)) != 0) {
        from_survivor.start = young_gen.start = h = (Header*)(ALIGN((size_t)p, PTR_SIZE));
        young_gen.size = DEFAULT_YOUNG_GEN_HEAP_SIZE;
        from_survivor.size = h->size = young_gen.size >> 1;
        FL_ALLOC_SET(h);

        to_survivor.start = (Header*)((void*)(from_survivor.start+1) + from_survivor.size);
        to_survivor.size = to_survivor.start->size = from_survivor.size;

    }else{
        abort();
    }

    old_gen_size = old_gen_size < DEFAULT_OLD_GEN_HEAP_SIZE ? DEFAULT_OLD_GEN_HEAP_SIZE : old_gen_size;
    //allocate old gen
    if ((q = sbrk(old_gen_size + PTR_SIZE + HEADER_SIZE)) != 0) {
        old_gen.start = h = (Header*)(ALIGN((size_t)q, PTR_SIZE));
        old_gen.size = h->size = DEFAULT_OLD_GEN_HEAP_SIZE;
        h->next_free = h;
        free_list = h;
    } else{
        abort();
    }
}

static void* ge_malloc(size_t req_size){
    Header *p;
    size_t do_gc = 0;

    req_size = ALIGN(req_size, PTR_SIZE);
    if (req_size < 0){
        // overflow
        abort();
    }

    for (;;) {
        p = from_survivor.start;
        if (p->size < req_size + HEADER_SIZE){
            if (!do_gc){
                minor_garbage_collect();
                do_gc = 1;
            } else {
                break;
            }
        }else {
            //allocate from from-survivor
            p->size -= (req_size + HEADER_SIZE);
            p = NEXT_HEADER(p);
            p->size = req_size;
            FL_ALLOC_SET(p);
            return (void*)(p+1);
        }
    }


    //allocate from old_gen
    return ge_malloc_in_old_gen(req_size);

}

static void* ge_malloc_in_old_gen(size_t req_size){
    //allocate in old_gen
    Header *p, *prev;
    size_t do_gc = 0;

    if (free_list == NULL){
        // if ((p = add_heap(req_size)) == NULL){
        //  abort();
        // }
        // prev = free_list = p;
        abort();
    }
    prev = free_list;
    for (p = prev->next_free; ; prev = p, p = p->next_free) {
        if (p->size >= req_size){
            if (p->size  == req_size + HEADER_SIZE){
                // size fit
                prev->next_free = p->next_free;
            }else {
                p->size -= (req_size + HEADER_SIZE);
                p = NEXT_HEADER(p);
                p->size = req_size;
            }
            free_list = prev;
            FL_ALLOC_SET(p);
            return (void*)(p+1);
        }

        if (p == free_list){
            if (!do_gc){
                // gc
                major_garbage_collect();
                do_gc = 1;
            }else{
                //grow heap
                need_grow_heap = 1;
                return NULL;
            }
        }
    }
}

void ge_free(void *p){
    Header *target, *hit;

    target = (Header *)p - 1;

    /* search join point of target to free_list */
    /* sorted by addr */
    for (hit = free_list; !(target > hit && target < hit->next_free); hit = hit->next_free)
        if (hit >= hit->next_free &&
            (target > hit || target < hit->next_free)){
            /* heap end And hit(search) */
            break;
        }

    if (NEXT_HEADER(target) == hit->next_free) {
        /* backward merge */
        target->size += (hit->next_free->size + HEADER_SIZE);
        target->next_free = hit->next_free->next_free;
    } else {
        /* join next free block */
        target->next_free = hit->next_free;
    }
    if (NEXT_HEADER(hit) == target) {
        /* forward merge */
        hit->size += (target->size + HEADER_SIZE);
        hit->next_free = target->next_free;
    } else {
        /* join before free block */
        hit->next_free = target;
    }
    free_list = hit;
    FL_ALLOC_CLEAR(target);
}


/* ========================================================================== */
/*  generational_gc                                                           */
/* ========================================================================== */

static struct root_range root;
static void *remember_set[RS_LIMIT];
static int rs_index = 0;
static struct gc_heap *hit_cache = NULL;

void init_root(void *start, void *end){
    if (NULL == start || NULL == end){
        abort();
    }

    if (start >= end){
        root.start = end;
        root.end = start;
    } else{
        root.start = start;
        root.end = end;
    }
}

void update_ptr(void *p, void *c, void **c_filed){
    Header *pa = (Header*)(p - HEADER_SIZE);

    if ((void*)c_filed < p || (void*)c_filed > (void*)NEXT_HEADER(pa)){
        // c_filed over limit
        abort();
    }

    if ((void*)pa >= (void*)old_gen.start
        && (void*)pa < (void*)NEXT_HEADER(old_gen.start)
        && !FL_TEST(p, FL_REMEMBER)) {
        remember_set[rs_index++] = (void*)pa;
        FL_REMEMBER_SET(p);
    }
    *c_filed = (void*)c;
}


static GCHeap * is_pointer_to_heap(void *ptr){
    if (hit_cache &&
        ((void *)hit_cache->start) <= ptr &&
        (size_t)ptr < (((size_t)hit_cache->start) + hit_cache->size))
        return hit_cache;

    if ((((void *)from_survivor.start) <= ptr) &&
        ((size_t)ptr < (((size_t)from_survivor.start) + from_survivor.size))) {
        hit_cache = &from_survivor;
        return &from_survivor;
    }

    if ((((void *)old_gen.start) <= ptr) &&
        ((size_t)ptr < (((size_t)old_gen.start) + old_gen.size))) {
        hit_cache = &old_gen;
        return &old_gen;
    }

    return NULL;
}

static Header* get_header(struct gc_heap *gh, void *ptr){
    Header *p, *pend, *pnext;

    pend = (Header *)(((size_t)gh->start) + gh->size);
    for (p = gh->start; p < pend; p = pnext) {
        pnext = NEXT_HEADER(p);
        if ((void *)(p+1) <= ptr && ptr < (void *)pnext) {
            return p;
        }
    }
    return NULL;
}

static void gc_mark_range(void *start, void *end);

static void gc_mark(void *ptr) {
    struct gc_heap *gh;
    Header *hdr;

    /* mark check */
    if (!(gh = is_pointer_to_heap(ptr))) return;
    if (!(hdr = get_header(gh, ptr))) return;
    if (!FL_TEST(hdr, FL_ALLOC)) return;
    if (FL_TEST(hdr, FL_MARK)) return;

    /* marking */
    FL_MARK_SET(hdr);

    /* mark children */
    gc_mark_range((void *)(hdr+1), (void *)NEXT_HEADER(hdr));
}

static void gc_mark_range(void *start, void *end){
    void *p;
    for (p = start; p < end; p++) {
        gc_mark(*(void **)p);
    }
}

static void gc_sweep() {
    Header *p, *pend;

    pend = (Header *)(((size_t)old_gen.start) + old_gen.size);
    for (p = old_gen.start; p < pend; p = NEXT_HEADER(p)) {
        if (FL_TEST(p, FL_ALLOC)) {
            if (FL_TEST(p, FL_MARK)) {
                FL_MARK_CLEAR(p);
            }
            else {
                ge_free(p + 1);
            }
        }
    }
}

void major_garbage_collect() {
    gc_mark_range(root.start, root.end);
    gc_sweep();
}

void promote(Header *p){
    void *q, *m, *n, *k;
    struct gc_heap *g;
    Header *h;

    q = ge_malloc_in_old_gen(p->size);
    if (q == NULL){
        abort();
    }

    memcpy(q - HEADER_SIZE, (void*)p, p->size + HEADER_SIZE);
    p->forwarding = (Header*)(q - HEADER_SIZE);
    FL_REMEMBER_SET(p);

    m = (void*)(p+1);
    for (n = m; n < m + p->size; n++) {
        k = *((void**)n);
        if (NULL == (g = is_pointer_to_heap(k))){
            continue;
        }
        if (NULL == (h = get_header(g, k))){
            continue;
        }
        if (!FL_TEST(h, FL_ALLOC)){
            continue;
        }

        if ((((void *)young_gen.start) <= k) &&
            ((size_t)k < (((size_t)young_gen.start) + young_gen.size))) {
            remember_set[rs_index++] = (void*)p->forwarding;
            FL_REMEMBER_SET(p->forwarding);
        }

    }
}

void * copy(Header *p) {
    Header *to, *h;
    void *q, *m, *n, *k;
    GCHeap *g;

    if (p == NULL){
        return NULL;
    }
    /* copy check */
    if (!(is_pointer_to_heap(p)))
        return NULL;
    if (!FL_TEST(p, FL_ALLOC))
        return NULL;

    if (!FL_TEST(p, FL_REMEMBER)){
        if (p->age < PROMOTE_AGE) {
            to = to_survivor.start;

            if (to->size >= (p->size + HEADER_SIZE)) {
                to->size -= (p->size + HEADER_SIZE);
                memcpy((void*)NEXT_HEADER(to), (void*)p, p->size + HEADER_SIZE);
                p->forwarding = NEXT_HEADER(to);
            } else{
                // allocate in old_gen
                q = ge_malloc_in_old_gen(p->size);
                memcpy((void*)(q - HEADER_SIZE), (void*)p, p->size + HEADER_SIZE);
                p->forwarding = (Header*)(q - HEADER_SIZE);
            }
            FL_REMEMBER_SET(p);
            p->forwarding->age++;

            //copy child
            m = (void*)(p->forwarding+1);
            for (n = m; n < m + p->size; n++) {
                k = (void*)(*((void**)n));
                if (NULL == (g = is_pointer_to_heap(k))){
                    continue;
                }
                if (NULL == (h = get_header(g, k))){
                    continue;
                }
                if (!FL_TEST(h, FL_ALLOC)){
                    continue;
                }
                k = copy((Header*)(k - HEADER_SIZE));
                if (NULL != k){
                    (*((void**)n)) = k;
                }
            }

        } else{
            promote(p);
        }
    }
    return (void*)(p->forwarding + 1);
}

void exchange_survivor(){
    struct gc_heap tmp;
    // exchange from_survivor and to_survivor
    tmp = from_survivor;
    from_survivor = to_survivor;
    to_survivor = tmp;
    // restore size
    to_survivor.start->size = to_survivor.size;
    FL_ALLOC_SET(from_survivor.start);
    FL_ALLOC_CLEAR(to_survivor.start);
}

void minor_garbage_collect(){

    // tranverse root
    void *p, *t;
    void *old_gen_start = (void*)old_gen.start;
    for (p = root.start; p < root.end; ++p){
        if (*((void **)p) < old_gen_start) {
            t = copy((Header*)((*((void **)p))- HEADER_SIZE));
            if (NULL != t){
                *((void **)p) = t;
            }
        }
    }

    // tranverse remember_set
    int index = 0;
    Header *h;
    while(index < rs_index){
        h = (Header*)remember_set[index];
        if (h != NULL){
            void *header_end;
            void *q;
            size_t has_obj_in_young_gen = 0;

            for (p = (void*)(h+1), header_end = (void*)NEXT_HEADER(h); p < header_end; ++p){
                q = *((void**)p);
                if (q < old_gen_start) {
                    q = copy(q - HEADER_SIZE);
                    if (q != NULL){
                        *((void **)p) = q;
                        if (q < old_gen_start){
                            has_obj_in_young_gen = 1;
                        }
                    }
                }
            }

            if (!has_obj_in_young_gen){
                FL_REMEMBER_CLEAR(h);
                rs_index--;
                remember_set[index] = remember_set[rs_index];
            } else{
                index++;
            }
        }
    }

    exchange_survivor();
}

void garbage_collect(){
    if (root.start != NULL){
        minor_garbage_collect();
        major_garbage_collect();
    }
}

/* ========================================================================== */
/*  test                                                                      */
/* ========================================================================== */
static void test_gc() {
    STACK_BASE(stack_base);
    STACK_TOP(stack_top);
    init_root((void*)stack_base, (void*)stack_top);

    void *p = ge_malloc(100);
    update_ptr(p, ge_malloc(200), (void**)p);
    assert(*((void**)p) == (void*)(NEXT_HEADER(from_survivor.start)+1));
    assert(NEXT_HEADER(NEXT_HEADER(from_survivor.start)) == (Header*)(p-HEADER_SIZE));

    void *k = ge_malloc(300);
    assert(k == (void*)(NEXT_HEADER(old_gen.start) + 1));

    void *m = ge_malloc(400);
    assert(m == (void*)(NEXT_HEADER(old_gen.start) + 1));

    void *n = ge_malloc(500);
    assert(n == (void*)(NEXT_HEADER(old_gen.start) + 1));
    assert((void*)(NEXT_HEADER(NEXT_HEADER(old_gen.start)) + 1) == *((void**)p));

    //major gc
    void *h = ge_malloc(600);
    assert(need_grow_heap == 1 && h == NULL);

    update_ptr(p, NULL, (void**)p);
    h = ge_malloc(600);
    assert(free_list->next_free != free_list);

    printf("%s\n", "finish test");
}

static void test(){
    init_gc_heap(DEFAULT_YOUNG_GEN_HEAP_SIZE, DEFAULT_OLD_GEN_HEAP_SIZE);
    test_gc();
}

int main(int argc, char **argv){
    if (argc == 2 && strcmp(argv[1], "test") == 0){
        test();
    }
    return 0;
}

