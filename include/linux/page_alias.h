#ifndef __LINUX_PAGE_ALIAS_H
#define __LINUX_PAGE_ALIAS_H

#include <linux/jump_label.h>

#ifdef CONFIG_PAGE_ALIAS

extern struct page_ext_operations page_alias_ops;
static struct page_alias* get_page_alias(struct page *page);

#endif
#endif
