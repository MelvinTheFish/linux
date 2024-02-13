#include <linux/interval_tree.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/rbtree.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/page_ext.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/migrate.h>
#include <linux/iommu.h>
#include <asm/page.h>
#include <linux/page_alias.h>

#include "internal.h"

struct page_alias{
        int do_not_move: 1;
        refcount_t ref_count;
        struct rmap_alias* rmap_list;
};

struct rmap_alias{
        struct rmap_alias* next;
        void* curr;
};

static inline struct page_alias* get_page_alias(struct page_ext *page_ext) 
{ 
        return page_ext_data(page_ext, &page_alias_ops);
} 

static __init bool need_page_alias(void)
{
        return true;
}

static __init void init_page_alias(void)
{
        printk(KERN_INFO "just one init will be enough");
}

struct page_ext_operations page_alias_ops = {
        .size = sizeof(struct page_alias),
        .need = need_page_alias,
        .init = init_page_alias,
};

static inline void __set_page_ext_alias(struct page_ext *page_ext)
{
	struct page_alias *page_alias;
        page_alias = get_page_alias(page_ext);
        page_alias->do_not_move = 1;
        refcount_set(&page_alias->ref_count, 1);
        // page_alias->rmap_list->next = NULL;
        // page_alias->rmap_list->curr = NULL;
}

noinline void __set_page_alias(struct page *page)
{
	//printk(KERN_ERR "omer and nizan: in set page_alias");
	struct page_ext *page_ext;
	page_ext = page_ext_get(page); //lock
	if (unlikely(!page_ext))
		return;
	__set_page_ext_alias(page_ext);
	page_ext_put(page_ext); //unlock
}
