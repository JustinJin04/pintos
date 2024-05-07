#include"spage.h"
#include <bitmap.h>

void swap_init(void);

/* 1. 将kpage内容拷贝到swap slot 2. 修改spage entry指向该swap slot*/
void swap_out(struct spage_table_entry* se);

/* 1. 将spage entry指向的swap slot内容拷贝到kpage 2. 修改spage entry 为原本指向（zero or file）*/
void swap_in(struct spage_table_entry* se);

void swap_free(size_t swap_index);