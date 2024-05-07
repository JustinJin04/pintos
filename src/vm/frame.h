#include "threads/palloc.h"
#include <stdbool.h>
#include <hash.h>
#include "threads/thread.h"

struct frame_table_entry{
    
    void* kpage;
    void* upage;    /* for kernel to reference the address using user virtual address instead of kernel virtual address*/

    struct thread* t;

    bool pin;

    struct hash_elem frame_elem;    /* embed into frame*/
};

void frame_init(void);
void* frame_allocate(enum palloc_flags flags, void* upage);
void frame_free (struct frame_table_entry* fe);
void frame_remove(void* kpage);

void frame_pin(void* kpage);
void frame_unpin(void* kpage);