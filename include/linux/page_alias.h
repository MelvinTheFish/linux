#ifndef __LINUX_PAGE_ALIAS_H
#define __LINUX_PAGE_ALIAS_H

#include <linux/jump_label.h>

#ifdef CONFIG_PAGE_ALIAS
extern void __set_page_alias(struct page *page);
extern struct page_ext_operations page_alias_ops;

static inline void set_page_alias(struct page *page)
{
        __set_page_alias(page);
}

#endif
#endif
