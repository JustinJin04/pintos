#ifndef SWAP_H
#define SWAP_H

#include"spage.h"
#include <bitmap.h>

void swap_init(void);
void swap_out(struct spage_table_entry* se);
void swap_in(struct spage_table_entry* se);
void swap_free(size_t swap_index);

#endif