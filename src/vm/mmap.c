#include "vm/mmap.h"
#include"vm/frame.h"
#include "filesys/file.h"
#include"threads/vaddr.h"
#include"userprog/syscall.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include <stdio.h>

static unsigned mmap_hash_func(const struct hash_elem *e, void *aux);
static bool mmap_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux);
static void mmap_destroy_func(struct hash_elem *e, void *aux);

mapid_t assign_mapid(){
    static mapid_t mapid = 0;
    return mapid++;
}

void mmap_debug(){
    printf("mmap_table\n");
    struct thread* cur = thread_current();
    struct hash* mmap_table = cur->mmap_table;
    struct hash_iterator i;
    hash_first(&i,mmap_table);
    while(hash_next(&i)){
        struct mmap_table_entry* me = hash_entry(hash_cur(&i), struct mmap_table_entry, mmap_elem);
        printf("fd : %d, size : %d, addr : %p\n",me->fd,me->size,me->addr);
    }
}

void
mmap_init(){
    struct thread* cur = thread_current();
    cur->mmap_table = (struct hash* )malloc(sizeof(struct hash));
    hash_init(cur->mmap_table,mmap_hash_func,mmap_less_func,NULL);
}

struct mmap_table_entry* 
mmap_find(struct thread* t, mapid_t mapping){
    
    struct mmap_table_entry me;
    me.fd = mapping;
    struct hash_elem* he = hash_find(t->mmap_table, &me.mmap_elem);
    if(he != NULL){
        return hash_entry(he,struct mmap_table_entry, mmap_elem);
    }
    return NULL;
}

/** 
 * 1. write back to disk
 * 2. frame free
*/
void 
mmap_write_back(struct spage_table_entry* se, bool keep_holding_lock){
    
    if(!lock_held_by_current_thread(&filesys_lock)){
        lock_acquire(&filesys_lock);
    }

    int size = file_write_at(se->file,se->kpage,se->read_bytes,se->offset);
    ASSERT(size>0);

    se->location = IN_FILESYS;
    if(!keep_holding_lock){
        lock_release(&filesys_lock);
    }
}

void
mmap_destroy(){
    struct thread* cur = thread_current();
    hash_destroy(cur->mmap_table, mmap_destroy_func);
    free(cur->mmap_table);
}

static unsigned 
mmap_hash_func(const struct hash_elem *e, void *aux UNUSED){
    struct mmap_table_entry *me = hash_entry(e, struct mmap_table_entry, mmap_elem);
    return hash_int(me->fd);
}

static bool 
mmap_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
    struct mmap_table_entry* me_a = hash_entry(a, struct mmap_table_entry, mmap_elem);
    struct mmap_table_entry* me_b = hash_entry(b, struct mmap_table_entry, mmap_elem);
    return me_a->fd < me_b->fd;
}

static void
mmap_destroy_func(struct hash_elem *e, void *aux UNUSED){
    struct mmap_table_entry* me = hash_entry(e, struct mmap_table_entry, mmap_elem);
    struct thread* cur = thread_current();

    void* upper_page = pg_round_up(me->addr + me->size);
    for(void* addr = me->addr;addr<upper_page;addr+=PGSIZE){
        struct spage_table_entry* se = spage_find(cur,addr);
        ASSERT(se&&se->type == MMAP);
        bool is_dirty = pagedir_is_dirty(cur->pagedir,addr);
        if(se->location == IN_FRAME){
            if(is_dirty){
                mmap_write_back(se, false);
            }
            frame_free(se->kpage,false);
        }
        spage_remove(se);
    }
    free(me);
}

void
mmap_remove(struct mmap_table_entry* me){
    struct hash_elem* he = hash_delete(thread_current()->mmap_table, &me->mmap_elem);
    if(he != NULL){
        free(hash_entry(he,struct mmap_table_entry, mmap_elem));
    }
}