#include <hash.h>
#include "threads/thread.h"
#include "vm/spage.h"

typedef int mapid_t;

struct mmap_table_entry{
    int fd;
    int size;
    void* addr;

    struct hash_elem mmap_elem;
};

void mmap_init(void);
struct mmap_table_entry* mmap_find(struct thread* t, mapid_t mapping);
void mmap_remove(struct mmap_table_entry*);
mapid_t assign_mapid(void);
void mmap_write_back(struct spage_table_entry* se, bool keep_holding_lock);
void mmap_destroy(void);
void mmap_debug(void);
