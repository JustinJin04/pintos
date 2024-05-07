#include "vm/swap.h"
#include "threads/vaddr.h"
#include "devices/block.h"

static struct bitmap* swap_table;
static struct block* swap_block;

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

void swap_init(){
    //swap_table = (struct bitmap*)malloc(sizeof(struct bitmap));
    swap_block = block_get_role(BLOCK_SWAP);
    ASSERT(swap_block != NULL);
    block_sector_t swap_size = block_size(swap_block);
    size_t swap_slots = swap_size / SECTORS_PER_PAGE;
    
    swap_table = bitmap_create(swap_slots);
    bitmap_set_all(swap_table, true);
}

void swap_out(struct spage_table_entry* se){
    /* se is anonymous page*/
    //ASSERT(se->status == IN_ZERO);
    //ASSERT(se->location == IN_FRAME);
    
    size_t swap_index = bitmap_scan_and_flip(swap_table, 0, 1, true);
    ASSERT(swap_index != BITMAP_ERROR);

    //printf("swap out index=%d, upage=%p, value=%x status= %d\n",swap_index,se->upage,*(int*)se->kpage,se->status);

    for(int i = 0; i < SECTORS_PER_PAGE; i++){
        block_write(swap_block, swap_index * SECTORS_PER_PAGE + i, se->kpage + i * BLOCK_SECTOR_SIZE);
    }
    se->location = IN_SWAP;
    se->swap_index = swap_index;
}

void swap_in(struct spage_table_entry* se){
    ASSERT(se->location == IN_SWAP);
    for(int i = 0; i < SECTORS_PER_PAGE; i++){
        block_read(swap_block, se->swap_index * SECTORS_PER_PAGE + i, se->kpage + i * BLOCK_SECTOR_SIZE);
    }
    //printf("swap in index=%d, upage=%p, value=%x status= %d\n",se->swap_index,se->upage,*(int*)se->kpage,se->status);
    //se->status = IN_ZERO;
    se->location = IN_FRAME;
    bitmap_set(swap_table, se->swap_index, true);
}

void swap_free(size_t swap_index){
    if(bitmap_test(swap_table,swap_index) == false){
        bitmap_set(swap_table,swap_index,true);
    }
}


