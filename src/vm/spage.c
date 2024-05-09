#include"vm/spage.h"
#include<hash.h>
#include"threads/palloc.h"
#include"threads/thread.h"
#include"threads/vaddr.h"
#include"userprog/pagedir.h"
#include"vm/frame.h"
#include"userprog/syscall.h"
#include "threads/malloc.h"
#include "string.h"
#include "vm/swap.h"
#include "filesys/file.h"

static unsigned spage_hash_func(const struct hash_elem *e, void *aux UNUSED);
static bool spage_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);
static void spage_destroy_func(struct hash_elem *e, void *aux UNUSED);

void
spage_init(){
    struct thread* cur = thread_current();
    cur->spage_table = (struct hash*)malloc(sizeof(struct hash));
    hash_init(cur->spage_table, spage_hash_func, spage_less_func, NULL);
}

/** insert a spage table entry pointed to a file*/
void
spage_install_file(void* upage, struct file* file, off_t offset, size_t read_bytes, size_t zero_bytes, bool writable){
    
    struct spage_table_entry* se = (struct spage_table_entry*)malloc(sizeof(struct spage_table_entry));
    ASSERT(se != NULL);
    
    /* initialize spage entry*/
    se->upage = upage;
    se->location = IN_FILESYS;
    se->type = REGU;
    se->file = file;
    se->offset = offset;
    se->read_bytes = read_bytes;
    se->zero_bytes = zero_bytes;
    se->writable = writable;

    /* insert into spage table*/
    hash_insert(thread_current()->spage_table, &se->spage_elem);
}

/** install MMap region*/
bool
spage_install_mmap(void* upage, struct file* file, off_t offset, size_t read_bytes, size_t zero_bytes, bool writable){

    struct spage_table_entry* se = (struct spage_table_entry*)malloc(sizeof(struct spage_table_entry));
    ASSERT(se != NULL);

    /* initialize spage entry*/
    se->upage = upage;
    se->location = IN_FILESYS;
    se->type = MMAP;
    se->file = file;
    se->offset = offset;
    se->read_bytes = read_bytes;
    se->zero_bytes = zero_bytes;
    se->writable = writable;
    
    struct thread* cur = thread_current();
    /* already mapped*/
    if(hash_find(cur->spage_table,&se->spage_elem)){
        ASSERT(0);
        return false;
    }
    hash_insert(cur->spage_table, &se->spage_elem);
    return true;
}

/** install a page that is filled with 0*/
void
spage_install_zero(void* upage,bool writable){
    struct spage_table_entry* se = (struct spage_table_entry*)malloc(sizeof(struct spage_table_entry));
    ASSERT(se != NULL);
    
    /* initialize spage entry*/
    se->upage = upage;
    se->location = IN_FILESYS;
    se->type = ANON;
    se->writable = writable;

    /* insert into spage table*/
    struct thread* cur = thread_current();
    hash_insert(cur->spage_table, &se->spage_elem);
}

/** load the page from file, swap or zero*/
bool 
spage_load(struct spage_table_entry* se){
    ASSERT(se != NULL);
    ASSERT(se->location != IN_FRAME);
    
    se->kpage = frame_allocate(PAL_USER, se->upage);
    if(se->kpage == NULL || pagedir_set_page(thread_current()->pagedir, se->upage, se->kpage, se->writable) == false){
        frame_free(se->kpage,false);
        return false;
    }

    switch(se->location){
        case IN_FRAME:
            ASSERT(0);
            break;
        case IN_FILESYS:
            ASSERT(pagedir_is_dirty(thread_current()->pagedir,se->upage)==false);
            if(se->type == REGU || se->type == MMAP){            
                lock_acquire(&filesys_lock);
                if(file_read_at(se->file, se->kpage, se->read_bytes, se->offset) != (int)se->read_bytes){
                    frame_free(se->kpage, false);
                    return false;
                }
                lock_release(&filesys_lock);
                memset(se->kpage + se->read_bytes, 0, se->zero_bytes);
                break;
            }
            else if(se->type == ANON){
                memset(se->kpage, 0, PGSIZE);
                break;
            }

        case IN_SWAP:
            pagedir_set_dirty(thread_current()->pagedir,se->upage,true);
            pagedir_set_accessed(thread_current()->pagedir,se->upage,true);
            swap_in(se);
            break;
    }
    se->location = IN_FRAME;
    /* warning: frame initialization done. need to unpin this frame*/
    frame_unpin(se->kpage);

    return true;
}

/** 
 * remove the spage table entry from spage table
 * WITHOUT freeing the frame entry or physical page or swap entry
*/
void 
spage_remove(struct spage_table_entry* se){
    struct hash_elem* he = hash_delete(thread_current()->spage_table, &se->spage_elem);
    if(he != NULL){
        free(hash_entry(he,struct spage_table_entry, spage_elem));
    }
}

/** destroy the spage_table along with frame allocated previous*/
void
spage_destroy(){
    struct thread* cur = thread_current();
    hash_destroy(cur->spage_table, spage_destroy_func);
    free(cur->spage_table);
}

/** find the spage table entry with upage from spage table*/
struct spage_table_entry* 
spage_find(struct thread* t, void* upage){

    struct spage_table_entry se;
    se.upage = upage;
    struct hash_elem* he = hash_find(t->spage_table, &se.spage_elem);
    if(he != NULL){
        return hash_entry(he, struct spage_table_entry, spage_elem);
    }
    return NULL;
}

/** utils functions*/
static unsigned spage_hash_func(const struct hash_elem *e, void *aux UNUSED){
    struct spage_table_entry *se = hash_entry(e, struct spage_table_entry, spage_elem);
    return hash_int((int)se->upage);
}

static bool spage_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
    struct spage_table_entry *se_a = hash_entry(a, struct spage_table_entry, spage_elem);
    struct spage_table_entry *se_b = hash_entry(b, struct spage_table_entry, spage_elem);
    return se_a->upage < se_b->upage;
}

static void spage_destroy_func(struct hash_elem *e, void *aux UNUSED){
    struct spage_table_entry *se = hash_entry(e, struct spage_table_entry, spage_elem);
    if(se->location == IN_FRAME){
        frame_free(se->kpage, false);
    }
    else if(se->location == IN_SWAP){
        swap_free(se->swap_index);
    }
    free(se);
}