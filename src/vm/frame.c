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

static struct hash frame_table;
static struct lock frame_lock;

static void frame_eviction(void);
static struct frame_table_entry* frame_select_victim(void);

/** utils functions*/
static unsigned frame_hash_func(const struct hash_elem *e, void *aux);
static bool frame_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux);

void debug_frame_table(){
    //lock_acquire(&frame_lock);
    printf("frame_table\n");
    for(int i=0;i<frame_table.bucket_cnt;++i){
        struct hash_elem* he = list_begin(&frame_table.buckets[i]);
        while(he != list_end(&frame_table.buckets[i])){
            struct frame_table_entry* fe = hash_entry(he, struct frame_table_entry, frame_elem);
            printf("kpage : %p, %d, upage : %p, %d, pin : %d\n", fe->kpage,pagedir_is_dirty(fe->t->pagedir,fe->kpage),
             fe->upage,pagedir_is_dirty(fe->t->pagedir,fe->upage),fe->pin);
            he = list_next(he);
        }
    }
    //lock_release(&frame_lock);
}


/** initialize the frame table*/
void
frame_init(){
    hash_init(&frame_table, frame_hash_func, frame_less_func, NULL);
    lock_init(&frame_lock);
}

/** allocate a frame combined with upage in current thread's address space. return kpage*/
void* 
frame_allocate(enum palloc_flags flags, void* upage){

    ASSERT(flags & PAL_USER);
    lock_acquire(&frame_lock);
    //printf("lock acquired by thread %d\n",thread_current()->tid);

    void* kpage = palloc_get_page(flags);
    
    /* frame is full and needs eviction*/
    if(kpage == NULL){
        //debug_frame_table();
        frame_eviction();
        kpage = palloc_get_page(flags);
    }
    ASSERT(kpage != NULL);
    struct frame_table_entry* fe = (struct frame_table_entry*)malloc(sizeof(struct frame_table_entry));
    
    fe->kpage = kpage;
    fe->upage = upage;
    fe->t = thread_current();
    fe->pin = false;
    
    hash_insert(&frame_table,&fe->frame_elem);
    
    //printf("lock released by thread %d, kpage = %x\n",thread_current()->tid,kpage);
    lock_release(&frame_lock);
    
    return kpage;
}

/** remove the frame_table_entry without freeing pages*/
void
frame_remove(void* kpage){
    struct frame_table_entry fe;
    fe.kpage = kpage;

    lock_acquire(&frame_lock);
    struct hash_elem* he = hash_delete(&frame_table, &fe.frame_elem);

    if(he != NULL){
        free(hash_entry(he, struct frame_table_entry, frame_elem));
    }

    lock_release(&frame_lock);
}

/** remove the frame_table_entry in frame_table and free pages allocated by palloc*/
void
frame_free(struct frame_table_entry* fe){
    
    ASSERT(lock_held_by_current_thread(&frame_lock));
    //lock_acquire(&frame_lock);
    palloc_free_page(fe->kpage);
    struct hash_elem* he = hash_delete(&frame_table, &fe->frame_elem);
    if(he != NULL){
        //free(hash_entry(he, struct frame_table_entry, frame_elem));
        free(fe);
    }
    //lock_release(&frame_lock);
}

/** evict a frame from frame_table and return the kpage*/
static void
frame_eviction(){
    
    ASSERT(lock_held_by_current_thread(&frame_lock));

    struct frame_table_entry* fe = frame_select_victim();
    ASSERT(fe != NULL);
    struct thread* t = fe->t;
    /* warning: the spage entry may not belong to current process's address space*/
    struct spage_table_entry* se = spage_find(t,fe->upage);
    bool is_dirty = pagedir_is_dirty(t->pagedir, fe->upage);
    //bool is_accessed = pagedir_is_accessed(cur->pagedir, fe->upage);

    pagedir_clear_page(t->pagedir, fe->upage);

    //ASSERT(is_dirty == pagedir_is_dirty(t->pagedir,fe->kpage));
    // if(is_dirty != pagedir_is_dirty(t->pagedir,fe->kpage)){
    //     //printf("content: %x\n",*(int*)fe->kpage);
    //     debug_frame_table();
    //     printf("upage: %d, kpage: %d\n",is_dirty,pagedir_is_dirty(t->pagedir,fe->kpage));
    //     ASSERT(0);
    // }
    if(is_dirty){
        /* write back to file*/
        //printf("is dirty %x\n",*(int*)se->kpage);
        // if(se->status == IN_FILESYS){
        //     //ASSERT(0);
        //     swap_out(se);
        // }
        // /* swap out*/
        // else{
        //     swap_out(se);
        // }
        swap_out(se);
    }
    else{
        /* ???????????????????????????*/
        //ASSERT(se->status != IN_SWAP);
        //swap_out(se);
        se->location = IN_FILESYS;
    }
    frame_free(fe);
}

static struct frame_table_entry* 
frame_select_victim(){
    //lock_acquire(&frame_lock);

    int total_frames = 0;
    for(int i=0;i<frame_table.bucket_cnt;++i){
        struct hash_elem* he = list_begin(&frame_table.buckets[i]);
        while(he != list_end(&frame_table.buckets[i])){
            total_frames++;
            he = list_next(he);
        }
    }

    if(total_frames == 0){
        //lock_release(&frame_lock);
        return NULL; // No frames to evict
    }

    while(1){
        int random_index = random_ulong() % total_frames;
        //printf("random_index : %d\n", random_index);
        total_frames = 0;

        for(int i=0;i<frame_table.bucket_cnt;++i){
            struct hash_elem* he = list_begin(&frame_table.buckets[i]);
            while(he != list_end(&frame_table.buckets[i])){
                if(total_frames == random_index && !hash_entry(he, struct frame_table_entry, frame_elem)->pin){
                    //lock_release(&frame_lock);
                    return hash_entry(he, struct frame_table_entry, frame_elem);
                }
                total_frames++;
                he = list_next(he);
            }
        }
    }
    //lock_release(&frame_lock);
    return NULL; // Should not reach here
}

void
frame_pin(void* kpage){
    struct frame_table_entry fe;
    fe.kpage = kpage;

    lock_acquire(&frame_lock);
    struct hash_elem* he = hash_find(&frame_table, &fe.frame_elem);

    if(he != NULL){
        struct frame_table_entry* fe = hash_entry(he, struct frame_table_entry, frame_elem);
        fe->pin = true;
    }
    lock_release(&frame_lock);
}

void
frame_unpin(void* kpage){
    struct frame_table_entry fe;
    fe.kpage = kpage;

    lock_acquire(&frame_lock);
    struct hash_elem* he = hash_find(&frame_table, &fe.frame_elem);

    if(he != NULL){
        struct frame_table_entry* fe = hash_entry(he, struct frame_table_entry, frame_elem);
        fe->pin = false;
    }
    lock_release(&frame_lock);
}


/** utils functions*/
static unsigned frame_hash_func(const struct hash_elem *e, void *aux){
    struct frame_table_entry *fe = hash_entry(e, struct frame_table_entry, frame_elem);
    return hash_int((int)fe->kpage);
}

static bool frame_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux){
    struct frame_table_entry *fea = hash_entry(a, struct frame_table_entry, frame_elem);
    struct frame_table_entry *feb = hash_entry(b, struct frame_table_entry, frame_elem);
    return fea->kpage < feb->kpage;
}