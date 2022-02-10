//
// Created by robq on 12/28/21.
//

#include <stdio.h>

#include "test.h"

int main() {
    init_test_heap();
    mem_allocate_test();
    one_block_free_test();
    two_blocks_free_test();
    new_region_extends_old_test();
    new_region_another_place_test();
    printf("=== All tests passed ===\n");
}
