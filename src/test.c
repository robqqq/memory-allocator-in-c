//
// Created by robq on 12/31/21.
//

#define _DEFAULT_SOURCE
#include "test.h"

#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>

#include "mem.h"
#include "mem_internals.h"
#include "util.h"

static void* heap;

static struct block_header* start_block;



//
//static size_t blocks_count () {
//    size_t count = 0;
//    for (struct block_header* curr = start_block; curr != NULL; curr = curr->next) {
//        count++;
//    }
//    return count;
//}


void init_test_heap(){
    heap = heap_init(10000);
    if (heap == NULL) {
        err ("err: error in heap initialization\n");
    } else {
        debug_heap (stderr, heap);
    }
    start_block = (struct block_header*) heap;
}

void mem_allocate_test() {
    debug("--- Start memory allocate test ---\n");
    void* test1_mem = _malloc(1000);
    if (test1_mem == NULL) {
        err ("err: memory allocate test failed\n");
    }
    debug_heap (stderr, heap);
    if (start_block->capacity.bytes != 1000 || start_block->is_free) {
        err ("err: memory allocate test failed\n");
    }
    debug ("Memory allocate test passed\n");
}

void one_block_free_test() {
    debug("--- Start one block free test ---\n");
    void* test2_mem = _malloc(1000);
    _malloc(1000);
    _free(test2_mem);
    debug_heap(stderr, heap);
    if (!((struct block_header*) (((uint8_t*) test2_mem)-offsetof(struct block_header, contents)))->is_free) {
        err("err: one block free test failed\n");
    }
    debug ("One block free test passed");
}

void two_blocks_free_test() {
    debug("--- Start two blocks free test ---\n");
    void* test3_mem1 = _malloc(1000);
    void* test3_mem2 = _malloc(1000);
    _free(test3_mem1);
    _free(test3_mem2);
    debug_heap(stderr, heap);
    if (!((struct block_header*) (((uint8_t*) test3_mem1)-offsetof(struct block_header, contents)))->is_free ||
            !((struct block_header*) (((uint8_t*) test3_mem2)-offsetof(struct block_header, contents)))->is_free) {
        err("err: two blocks free test failed\n");
    }
    debug ("Two block free test passed\n");
}

static void free_heap() {
    struct block_header* curr = start_block;
    while (curr != NULL) {
        if (!curr->is_free) {
            _free((void*) (((uint8_t*) curr) + offsetof(struct block_header, contents)));
            curr = curr->next;
        }
    }
}

void new_region_extends_old_test() {
    debug ("--- Start new region extends old test ---\n");

    free_heap();

    void* test4_mem = _malloc(20000);

    debug_heap(stderr, heap);

    if (((struct block_header*) (((uint8_t*) test4_mem)-offsetof(struct block_header, contents))) != heap ||
            ((struct block_header*) (((uint8_t*) test4_mem)-offsetof(struct block_header, contents)))->is_free ||
                                                                                                                                                                                                ((struct block_header*) (((uint8_t*) test4_mem)-offsetof(struct block_header, contents)))->capacity.bytes != 20000) {
        err ("err: new region extends old test failed\n");
    }
    debug("New region extends old test passed\n");
}

struct block_header* last_block() {
    for (struct block_header* last = start_block; ; last = last->next) {
        if (last->next == NULL) {
            return last;
        }
    }
}

void new_region_another_place_test() {
    debug ("--- Start new region another place test ---\n");

    struct block_header* last = last_block();

    void* test5_taken_mem_addr = last + size_from_capacity(last->capacity).bytes;

    void* test5_taken_mem = mmap( (uint8_t*) (getpagesize() * ((size_t) test5_taken_mem_addr / getpagesize()
            + (((size_t) test5_taken_mem_addr % getpagesize()) > 0))),1000, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);

    debug("test taken mem addr: %p\n", test5_taken_mem);

    void* test5_mem = _malloc(40000);

    debug_heap(stderr, heap);

    if (test5_mem == start_block->next || test5_mem == test5_taken_mem ||
        ((struct block_header*) (((uint8_t*) test5_mem)-offsetof(struct block_header, contents)))->capacity.bytes != 40000 ||
                ((struct block_header*) (((uint8_t*) test5_mem)-offsetof(struct block_header, contents)))->is_free) {
        err("err: new region another place test failed\n");
    }
    debug ("New region another place test passed\n");
}
