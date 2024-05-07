#include"vm/spage.h"
#include<hash.h>
#include"threads/palloc.h"
#include"threads/thread.h"
#include"threads/vaddr.h"
#include"userprog/pagedir.h"
#include"vm/frame.h"
//#include"vm/swap.h"
//#include"vm/swap.h"

static unsigned spage_hash_func(const struct hash_elem *e, void *aux);
static bool spage_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux);
static void spage_destroy_func(struct hash_elem *e, void *aux);

void
spage_init(){
    struct thread* cur = thread_current();
    cur->spage_table = (struct hash*)malloc(sizeof(struct hash));
    hash_init(cur->spage_table, spage_hash_func, spage_less_func, NULL);
}

/** insert a spage table entry pointed to a file*/
void
spage_install_file(void* upage, struct file* file, off_t offset, size_t read_bytes, size_t zero_bytes, bool writable){
    //ASSERT(spage_find(upage) == NULL);
    struct spage_table_entry* se = (struct spage_table_entry*)malloc(sizeof(struct spage_table_entry));
    se->upage = upage;
    //se->status = IN_FILESYS;
    se->location = IN_FILESYS;
    se->type = REGU;
    se->file = file;
    se->offset = offset;
    se->read_bytes = read_bytes;
    se->zero_bytes = zero_bytes;
    se->writable = writable;
    struct thread* cur = thread_current();
    hash_insert(cur->spage_table, &se->spage_elem);
}

/** install a page that's already on the frame*/
void
spage_install_frame(void* upage, void* kpage,bool writable){
    //ASSERT(spage_find(upage) == NULL);
    struct spage_table_entry* se = (struct spage_table_entry*)malloc(sizeof(struct spage_table_entry));
    se->upage = upage;
    se->kpage = kpage;
    //se->status = IN_FRAME;
    se->writable = writable;
    struct thread* cur = thread_current();
    hash_insert(cur->spage_table, &se->spage_elem);
}

/** install a page that is filled with 0*/
void
spage_install_zero(void* upage,bool writable){
    //ASSERT(spage_find(upage) == NULL);
    struct spage_table_entry* se = (struct spage_table_entry*)malloc(sizeof(struct spage_table_entry));
    se->upage = upage;
    //se->status = IN_ZERO;
    se->location = IN_FILESYS;
    se->type = ANON;
    se->writable = writable;
    struct thread* cur = thread_current();
    hash_insert(cur->spage_table, &se->spage_elem);
}

/** load the page from file, swap or zero*/
bool 
spage_load(struct spage_table_entry* se){
    ASSERT(se != NULL);
    if(se->location == IN_FRAME){
        return true;
    }
    // if(pagedir_get_page(thread_current()->pagedir,se->upage)){
    //     return true;
    // }

    se->kpage = frame_allocate(PAL_USER, se->upage);
    ASSERT(se->kpage != NULL);

        //se->status = IN_FRAME;
    if(pagedir_set_page(thread_current()->pagedir, se->upage, se->kpage, se->writable) == false){
        frame_free(se);
        return false;
    }
    // ASSERT(pagedir_is_dirty(thread_current()->pagedir,se->kpage)==false);

    switch(se->location){
        case IN_FILESYS:
            if(se->type == REGU){
                ASSERT(pagedir_is_dirty(thread_current()->pagedir,se->upage)==false);
                file_seek(se->file, se->offset);
                if(file_read(se->file, se->kpage, se->read_bytes) != (int)se->read_bytes){
                    frame_free(se);
                    return false;
                }
                memset(se->kpage + se->read_bytes, 0, se->zero_bytes);
                break;
            }
            else if(se->type == ANON){
                ASSERT(pagedir_is_dirty(thread_current()->pagedir,se->upage)==false);
                memset(se->kpage, 0, PGSIZE);
                break;
            }
        case IN_SWAP:
            pagedir_set_dirty(thread_current()->pagedir,se->upage,true);
            swap_in(se);
            break;
    }
    se->location = IN_FRAME;

    // //se->status = IN_FRAME;
    // if(pagedir_set_page(thread_current()->pagedir, se->upage, se->kpage, se->writable) == false){
    //     frame_free(se);
    //     return false;
    // }

    return true;
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
static unsigned spage_hash_func(const struct hash_elem *e, void *aux){
    struct spage_table_entry *se = hash_entry(e, struct spage_table_entry, spage_elem);
    return hash_int((int)se->upage);
}

static bool spage_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux){
    struct spage_table_entry *se_a = hash_entry(a, struct spage_table_entry, spage_elem);
    struct spage_table_entry *se_b = hash_entry(b, struct spage_table_entry, spage_elem);
    return se_a->upage < se_b->upage;
}

static void spage_destroy_func(struct hash_elem *e, void *aux){
    struct spage_table_entry *se = hash_entry(e, struct spage_table_entry, spage_elem);
    // if(se->status == IN_FRAME){
    //     frame_remove(se->kpage);
    // }

    /* 注意spage table上的entry不一定都在frame上，可能在file或swap*/
    // frame_remove(se->kpage);
    // if(se->status == IN_SWAP)   swap_free(se->swap_index);
    if(se->location == IN_FRAME){
        frame_remove(se->kpage);
    }
    else if(se->location == IN_SWAP){
        swap_free(se->swap_index);
    }
    
    free(se);
}