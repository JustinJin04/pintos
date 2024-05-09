#include"frame.h"
#include"threads/palloc.h"
#include"threads/malloc.h"
#include"threads/thread.h"
#include"threads/vaddr.h"
#include"threads/synch.h"
#include"userprog/pagedir.h"
#include"vm/swap.h"
#include <string.h>
#include"lib/random.h"
#include"vm/mmap.h"
#include <stdio.h>

struct frame_table{

    struct hash hash_table;         /* hash table easy to search*/
    
    struct list clock_list;         /* implement clock replacement algorithm*/
    struct list_elem* clock_ptr;

    struct lock frame_lock;
};

struct frame_table frame_table;     /* shared by all processes*/

static void frame_eviction(void);
static struct frame_table_entry* frame_select_victim_clock(void);
static struct frame_table_entry* frame_select_victim_random(void);

/** utils functions*/
static unsigned frame_hash_func(const struct hash_elem *e, void *aux UNUSED);
static bool frame_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);

void frame_debug(){
    printf("frame_table\n");
    struct hash_iterator i;
    hash_first(&i,&frame_table.hash_table);
    while(hash_next(&i)){
        struct frame_table_entry* fe = hash_entry(hash_cur(&i), struct frame_table_entry, frame_hash_elem);
        struct spage_table_entry* se = spage_find(fe->t,fe->upage);
        printf("kpage : %p, upage : %p, tid : %d, location: %d\n",fe->kpage,fe->upage,fe->t->tid, se->location);
    }
}

/** initialize the frame table*/
void
frame_init(){
    hash_init(&frame_table.hash_table, frame_hash_func, frame_less_func, NULL);
    list_init(&frame_table.clock_list);
    frame_table.clock_ptr = NULL;
    lock_init(&frame_table.frame_lock);
}

/** allocate a frame combined with upage in current thread's address space. return kpage*/
void* 
frame_allocate(enum palloc_flags flags, void* upage){

    ASSERT(flags & PAL_USER);
    lock_acquire(&frame_table.frame_lock);

    /* allocate a physical page*/
    void* kpage = palloc_get_page(flags);
    /* need eviction*/
    if(kpage == NULL){
        frame_eviction();
        kpage = palloc_get_page(flags);
    }

    /* initialize frame entry*/
    ASSERT(kpage != NULL);
    struct frame_table_entry* fe = (struct frame_table_entry*)malloc(sizeof(struct frame_table_entry));
    fe->kpage = kpage;
    fe->upage = upage;
    fe->t = thread_current();
    /* warning: before the frame was initialized by process, it must stay at the frame table.*/
    fe->pin = true;
    
    /* insert into frame_table*/
    hash_insert(&frame_table.hash_table,&fe->frame_hash_elem);
    list_push_back(&frame_table.clock_list, &fe->frame_list_elem);
    ASSERT(hash_size(&frame_table.hash_table) == list_size(&frame_table.clock_list));

    lock_release(&frame_table.frame_lock);
    return kpage;
}

/** free the kpage associated with frame entry and remove from frame table*/
void
frame_free(void* kpage, bool keep_holding_lock){
    
    if(!lock_held_by_current_thread(&frame_table.frame_lock)){
        lock_acquire(&frame_table.frame_lock);
    }

    struct frame_table_entry fe;
    fe.kpage = kpage;

    /* remove from hash table*/
    struct hash_elem* he = hash_delete(&frame_table.hash_table, &fe.frame_hash_elem);
    ASSERT(he != NULL);
    struct frame_table_entry* fte = hash_entry(he, struct frame_table_entry, frame_hash_elem);
    
    /* remove from clock list*/
    list_remove(&fte->frame_list_elem);

    /* free physical page*/
    palloc_free_page(kpage);
    pagedir_clear_page(fte->t->pagedir, fte->upage);

    /* free entry memory*/
    free(fte);

    if(!keep_holding_lock){
        lock_release(&frame_table.frame_lock);
    }
}

/** evict a frame from frame_table and return the kpage*/
static void
frame_eviction(){
    ASSERT(lock_held_by_current_thread(&frame_table.frame_lock));

    struct frame_table_entry* fe = frame_select_victim_clock();
    ASSERT(fe != NULL);
    
    struct thread* t = fe->t;
    /* warning: the spage entry may not belong to current process's address space*/
    struct spage_table_entry* se = spage_find(t,fe->upage);
    bool is_dirty = pagedir_is_dirty(t->pagedir, fe->upage);
    ASSERT(se ->location == IN_FRAME);
    if(is_dirty){
        /* write back to file*/
        if(se->type == MMAP){
            mmap_write_back(se, false);
        }
        /* swap space swapping*/
        else{
            swap_out(se);
        }
    }
    else{
        se->location = IN_FILESYS;
    }
    
    /* free the frame entry and the physical page*/
    frame_free(fe->kpage,true);
}

static struct frame_table_entry*
frame_select_victim_clock(){
    struct list_elem* e = (frame_table.clock_ptr == NULL || frame_table.clock_ptr==list_end(&frame_table.clock_list)) ?
                            list_begin(&frame_table.clock_list) : frame_table.clock_ptr;
    while(1){
        if(e == list_end(&frame_table.clock_list)){
            e = list_begin(&frame_table.clock_list);
        }
        struct frame_table_entry* fe = list_entry(e, struct frame_table_entry, frame_list_elem);
        if(fe->pin == false && !pagedir_is_accessed(fe->t->pagedir, fe->upage)){
            frame_table.clock_ptr = list_next(e);
            return fe;
        }
        else{
            pagedir_set_accessed(fe->t->pagedir, fe->upage, false);
            e = list_next(e);
        }
    }
}

static struct frame_table_entry* 
frame_select_victim_random(){

    int total_frames = hash_size(&frame_table.hash_table);
    if(total_frames == 0){
        return NULL;
    }
    int count =0;
    while(1){
        count++;
        if(count>10){
            ASSERT(0);
        }
        int random_index = random_ulong() % total_frames;
        total_frames = 0;

        for(int i=0;i<frame_table.hash_table.bucket_cnt;++i){
            struct hash_elem* he = list_begin(&frame_table.hash_table.buckets[i]);
            while(he != list_end(&frame_table.hash_table.buckets[i])){
                if(total_frames == random_index && !hash_entry(he, struct frame_table_entry, frame_hash_elem)->pin){
                    return hash_entry(he, struct frame_table_entry, frame_hash_elem);
                }
                total_frames++;
                he = list_next(he);
            }
        }
    }
    return NULL;
}

void
frame_pin(void* kpage){
    struct frame_table_entry fe;
    fe.kpage = kpage;

    lock_acquire(&frame_table.frame_lock);
    struct hash_elem* he = hash_find(&frame_table.hash_table, &fe.frame_hash_elem);

    if(he != NULL){
        struct frame_table_entry* fe = hash_entry(he, struct frame_table_entry, frame_hash_elem);
        fe->pin = true;
    }
    lock_release(&frame_table.frame_lock);
}

void
frame_unpin(void* kpage){
    struct frame_table_entry fe;
    fe.kpage = kpage;

    lock_acquire(&frame_table.frame_lock);
    struct hash_elem* he = hash_find(&frame_table.hash_table, &fe.frame_hash_elem);

    if(he != NULL){
        struct frame_table_entry* fe = hash_entry(he, struct frame_table_entry, frame_hash_elem);
        fe->pin = false;
    }
    lock_release(&frame_table.frame_lock);
}


/** utils functions*/
static unsigned frame_hash_func(const struct hash_elem *e, void *aux UNUSED){
    struct frame_table_entry *fe = hash_entry(e, struct frame_table_entry, frame_hash_elem);
    return hash_int((int)fe->kpage);
}

static bool frame_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
    struct frame_table_entry *fea = hash_entry(a, struct frame_table_entry, frame_hash_elem);
    struct frame_table_entry *feb = hash_entry(b, struct frame_table_entry, frame_hash_elem);
    return fea->kpage < feb->kpage;
}