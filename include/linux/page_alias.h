#ifndef __LINUX_PAGE_ALIAS_H
#define __LINUX_PAGE_ALIAS_H

#include <linux/jump_label.h>

#ifdef CONFIG_PAGE_ALIAS
extern void __set_page_alias(struct page *page);
extern struct page_ext_operations page_alias_ops;
struct page* alias_vmap_to_page(void* p);
void* alias_vmap(struct page **pages, int n);
void alias_vunmap(void* p);
void alias_page_close(struct page* page);
void add_to_alias_rmap(struct page* page, void* ptr);
void check_migration_at_start(struct list_head *from);
int is_alias_rmap_empty(struct page* page);
void* get_alias_rmap(struct page* page);

static inline void set_page_alias(struct page *page)
{
       __set_page_alias(page);
}

#endif
#endif
