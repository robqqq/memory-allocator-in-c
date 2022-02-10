#define _DEFAULT_SOURCE

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "mem_internals.h"
#include "mem.h"
#include "util.h"

void debug_block(struct block_header* b, const char* fmt, ... );

extern inline block_size size_from_capacity( block_capacity cap );
extern inline block_capacity capacity_from_size( block_size sz );

static block_size get_block_size(size_t bytes) {
    return (block_size) {
        .bytes = bytes
    };
}

static block_capacity get_block_capacity(size_t bytes) {
    return (block_capacity) {
        .bytes = bytes
    };
}

static bool block_is_big_enough( size_t query, struct block_header* block ) {
    return block->capacity.bytes >= query;
}

static size_t pages_count ( size_t mem ) {
    return mem / getpagesize() + ((mem % getpagesize()) > 0);
}

static size_t round_pages ( size_t mem ) {
    return getpagesize() * pages_count( mem );
}

static void block_init( void* restrict addr, block_size block_sz, void* restrict next ) {
    *((struct block_header*)addr) = (struct block_header) {
        .next = next,
        .capacity = capacity_from_size(block_sz),
        .is_free = true
    };
}

static size_t region_actual_size( size_t query ) {
    return size_max( round_pages( query ), REGION_MIN_SIZE );
}

extern inline bool region_is_invalid( const struct region* r );



static void* map_pages(void const* addr, size_t length, int additional_flags) {
    return mmap( (void*) addr, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | additional_flags , -1, 0 );
}

/*  аллоцировать регион памяти и инициализировать его блоком */
static struct region alloc_region  ( void const * addr, size_t query ) {
    query = region_actual_size(query + offsetof(struct block_header, contents));
    struct region region = {
            .addr = map_pages(addr, query, MAP_FIXED_NOREPLACE),
            .extends = true,
            .size = query
    };
    if (region_is_invalid(&region) || region.addr == MAP_FAILED) {
        region.addr = map_pages(addr, query, 0);
        region.extends = false;
        if (region_is_invalid(&region) || region.addr == MAP_FAILED) {
            return REGION_INVALID;
        }
    }
    block_init (region.addr, get_block_size(region.size), NULL);
    return region;
}

static void* block_after( struct block_header const* block );

void* heap_init( size_t initial ) {
    const struct region region = alloc_region( HEAP_START, initial );
    if ( region_is_invalid(&region) ) return NULL;

    return region.addr;
}

#define BLOCK_MIN_CAPACITY 24

/*  --- Разделение блоков (если найденный свободный блок слишком большой )--- */

static bool block_splittable( struct block_header* restrict block, size_t query) {
    return block-> is_free && query + offsetof( struct block_header, contents ) + BLOCK_MIN_CAPACITY <= block->capacity.bytes;
}

static bool split_if_too_big( struct block_header* block, size_t query ) {
    if (block_splittable(block, query)) {
        block_size snd_size =  get_block_size(block->capacity.bytes - query);
        void* addr = (uint8_t*) (block) + size_from_capacity(get_block_capacity(query)).bytes;
        block_init (addr, snd_size, block->next);
        block->next = addr;
        block->capacity = get_block_capacity(query);
        return true;
    }
    return false;
}


/*  --- Слияние соседних свободных блоков --- */

static void* block_after( struct block_header const* block )              {
    return  (void*) (block->contents + block->capacity.bytes);
}
static bool blocks_continuous (
                               struct block_header const* fst,
                               struct block_header const* snd ) {
  return (void*)snd == block_after(fst);
}

static bool mergeable(struct block_header const* restrict fst, struct block_header const* restrict snd) {
    return fst->is_free && snd->is_free && blocks_continuous( fst, snd ) ;
}

static bool try_merge_with_next( struct block_header* block ) {
    if (block != NULL && block->next != NULL && mergeable(block, block->next)){
        block->capacity.bytes += size_from_capacity(block->next->capacity).bytes;
        block->next = block->next->next;
        return true;
    }
    return false;
}


/*  --- ... ecли размера кучи хватает --- */

struct block_search_result {
    enum {BSR_FOUND_GOOD_BLOCK, BSR_REACHED_END_NOT_FOUND, BSR_CORRUPTED} type;
    struct block_header* block;
};


static struct block_search_result find_good_or_last  ( struct block_header* restrict block, size_t sz )    {
    if (!block) {
        return (struct block_search_result) {
            .type = BSR_CORRUPTED,
            .block = NULL
        };
    }
    struct block_header* curr = block;
    struct block_header* last;
    while (curr) {
        while (try_merge_with_next(curr));
        if(curr->is_free && block_is_big_enough(sz, curr)) {
            return (struct block_search_result) {
                    .type = BSR_FOUND_GOOD_BLOCK,
                    .block = curr
            };
        }
        last = curr;
        curr = curr->next;
    }
    return (struct block_search_result) {
            .type = BSR_REACHED_END_NOT_FOUND,
            .block = last
    };
}

/*  Попробовать выделить память в куче начиная с блока `block` не пытаясь расширить кучу
 Можно переиспользовать как только кучу расширили. */
static struct block_search_result try_memalloc_existing ( size_t query, struct block_header* block ) {
    struct block_search_result bsr = find_good_or_last(block, query);
    if (!bsr.type) {
        split_if_too_big(bsr.block, query);
        bsr.block->is_free = false;
    }
    return bsr;
}

static struct block_header* grow_heap( struct block_header* restrict last, size_t query ) {
    if (!last) return NULL;
    struct region new = alloc_region(block_after(last), query + offsetof(struct block_header, contents));
    if (region_is_invalid(&new)) return NULL;
    last->next = new.addr;
    if (try_merge_with_next(last)) {
        return last;
    }
    return last->next;
}

/*  Реализует основную логику malloc и возвращает заголовок выделенного блока */
static struct block_header* memalloc( size_t query, struct block_header* heap_start) {
    if (query < BLOCK_MIN_CAPACITY) {
        query = BLOCK_MIN_CAPACITY;
    }
    struct block_search_result bsr = try_memalloc_existing(query, heap_start);
    if (bsr.type == BSR_REACHED_END_NOT_FOUND) {
        struct block_header* new = grow_heap(bsr.block, query);
        if (!new) return NULL;
        bsr = try_memalloc_existing(query, new);
    }
    if (bsr.type != BSR_FOUND_GOOD_BLOCK) return NULL;
    return bsr.block;
}

void* _malloc( size_t query ) {
    struct block_header* const addr = memalloc( query, (struct block_header*) HEAP_START );
    if (addr) return addr->contents;
    else return NULL;
}

static struct block_header* block_get_header(void* contents) {
    return (struct block_header*) (((uint8_t*)contents)-offsetof(struct block_header, contents));
}

void _free( void* mem ) {
    if (!mem) return ;
    struct block_header* header = block_get_header( mem );
    header->is_free = true;
    while (header != NULL) {
        try_merge_with_next(header);
        header = header->next;
    }
}
