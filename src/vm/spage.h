#include <hash.h>
#include"threads/thread.h"
#include "filesys/off_t.h"

enum page_locations{
    //IN_FRAME,
    IN_FILESYS,
    //IN_ZERO,
    
    IN_SWAP,
    IN_FRAME

};

enum page_types{
    REGU,
    ANON
};

struct spage_table_entry{
    void* upage;
    void* kpage;
    //enum page_status status;
    enum page_locations location;
    enum page_types type;

    /* used for swap*/
    size_t swap_index;
    
    /* used for file*/
    struct file* file;
    off_t offset;
    size_t read_bytes;
    size_t zero_bytes;
    bool writable;

    struct hash_elem spage_elem;
};

void spage_init(void);
//void spage_insert(void* upage, void* kpage, enum page_status status, struct file* file, off_t offset, size_t read_bytes, size_t zero_bytes, bool writable);
void spage_install_file(void* upage, struct file* file, off_t offset, size_t read_bytes, size_t zero_bytes, bool writable);
void spage_install_frame(void* upage, void* kpage,bool writable);
void spage_install_zero(void* upage,bool writable);
void spage_install_swap(void* upage, int swap_index);

bool spage_load(struct spage_table_entry* se);

//void spage_remove(void* upage);
void spage_destroy(void);
struct spage_table_entry* spage_find(struct thread* t,void* upage);
