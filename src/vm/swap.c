#include "vm/swap.h"
#include "threads/vaddr.h"
#include "devices/block.h"

static struct bitmap* swap_table;
static struct block* swap_block;

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

void swap_init(){
    swap_block = block_get_role(BLOCK_SWAP);
    ASSERT(swap_block != NULL);
    block_sector_t swap_size = block_size(swap_block);
    size_t swap_slots = swap_size / SECTORS_PER_PAGE;
    
    swap_table = bitmap_create(swap_slots);
    bitmap_set_all(swap_table, true);
}

/* write the content of kpage of se to swap space and update se entry
* WITHOUT freeing the frame entry
*/
void swap_out(struct spage_table_entry* se){
    size_t swap_index = bitmap_scan_and_flip(swap_table, 0, 1, true);
    ASSERT(swap_index != BITMAP_ERROR);

    for(int i = 0; i < SECTORS_PER_PAGE; i++){
        block_write(swap_block, swap_index * SECTORS_PER_PAGE + i, se->kpage + i * BLOCK_SECTOR_SIZE);
    }
    se->location = IN_SWAP;
    se->swap_index = swap_index;
}

/* write the content of swap space into the kpage of se
* AND freeing the swap entry
*/
void swap_in(struct spage_table_entry* se){
    ASSERT(se->location == IN_SWAP);
    for(int i = 0; i < SECTORS_PER_PAGE; i++){
        block_read(swap_block, se->swap_index * SECTORS_PER_PAGE + i, se->kpage + i * BLOCK_SECTOR_SIZE);
    }
    se->location = IN_FRAME;
    bitmap_set(swap_table, se->swap_index, true);
}

/* free the swap entry
*/
void swap_free(size_t swap_index){
    if(bitmap_test(swap_table,swap_index) == false){
        bitmap_set(swap_table,swap_index,true);
    }
}


