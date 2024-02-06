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
        int number;
};

static __init bool need_page_alias(void)
{
        return true;
}

static __init void init_page_alias(void)
{
        printk(KERN_ERR "just one init will be enough");
}

struct page_ext_operations page_alias_ops = {
        .size = sizeof(struct page_alias),
        .need = need_page_alias,
        .init = init_page_alias,
};
