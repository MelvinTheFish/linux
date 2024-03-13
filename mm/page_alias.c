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

#define KERNEL_RMAP 0
#define IOMMU_RMAP 1

struct rmap_alias {
	struct rmap_alias *next;
	void *curr;
	int type; //0 is kernel, 1 is iommu
};

#define for_each_alias_rmap(rmap, alias) \
    for (rmap = alias->iommu_rmap_list; rmap->curr; rmap = rmap->next)
struct page_alias {
	atomic_t do_not_move;
	atomic_t kernel_ref_count;
	struct rmap_alias *iommu_rmap_list;
	void* kernel_rmap;
};

static inline struct page_alias *get_page_alias(struct page_ext *page_ext)
{
	return page_ext_data(page_ext, &page_alias_ops);
}

static __init bool need_page_alias(void)
{
	return true;
}

static __init void init_page_alias(void)
{
	printk(KERN_INFO "just one init will be enough\n");
}

struct page_ext_operations page_alias_ops = {
	.size = sizeof(struct page_alias),
	.need = need_page_alias,
	.init = init_page_alias,
};

static noinline void __set_page_ext_alias(struct page_ext *page_ext)
{
	struct page_alias *page_alias;
	page_alias = get_page_alias(page_ext);
	atomic_set(&page_alias->do_not_move, 0);
	atomic_set(&page_alias->kernel_ref_count, 0);
	page_alias->iommu_rmap_list = NULL;
	page_alias->kernel_rmap = NULL;
}


noinline void __set_page_alias(struct page *page)
{
	struct page_ext *page_ext;
	page_ext = page_ext_get(page); //lock
	if (unlikely(!page_ext))
		return;
	__set_page_ext_alias(page_ext);
	page_ext_put(page_ext); //unlock
}

void *alias_vmap(struct page *page)
{
	/* create a kernel alias for a single page if it 
	 * doesn't already exist and return a 
	 * pointer to its vmap
	 */
	void *vmap_address;
	BUG_ON(!page);
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias =
		page_ext_data(page_ext, &page_alias_ops);
	vmap_address = page_alias->kernel_rmap;
	if(atomic_inc_not_zero(&page_alias->kernel_ref_count)){
		/* could also always skip this, but it could save unecessary vmaps and vunmaps */
		pr_info("ref count is not zero\n");
		BUG_ON(!vmap_address);
	} else{
		/* kernerl_ref_count was zero, so need to create a vmap */
		pr_info("in %s, doing doing vmap\n", __func__);
		page_ext_put(page_ext); /* because vmap might sleep */
		vmap_address = vmap(&page, 1, VM_MAP, PAGE_KERNEL);
		pr_info("in %s, doing try_cmpxchg\n", __func__);
		page_ext = page_ext_get(page);
		page_alias = page_ext_data(page_ext, &page_alias_ops);
		if (!try_cmpxchg(&page_alias->kernel_rmap, (void*)NULL, vmap_address)){
		/* someone else created the vmap before us */
			page_ext_put(page_ext); /* because vmap might sleep */
			vunmap(vmap_address);
			page_ext = page_ext_get(page);
			page_alias = page_ext_data(page_ext, &page_alias_ops);
			vmap_address = page_alias->kernel_rmap;
			BUG_ON(!vmap_address);
		}
		atomic_inc(&page_alias->kernel_ref_count);
	}
	 /* if it's zero, bug. */
	page_ext_put(page_ext);
	return vmap_address;
}

void alias_vunmap(void *p)
{
	struct page* page = alias_vmap_to_page(p);
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias =
		page_ext_data(page_ext, &page_alias_ops);
	if (!atomic_add_unless(&page_alias->kernel_ref_count, -1, 1)){
		if(atomic_cmpxchg(&page_alias->kernel_ref_count, 1, 0))
			vunmap(p);
		else
		/* Either someone else unmapped, or someone else added a referance. */
			atomic_dec(&page_alias->kernel_ref_count);
	}
	p = NULL;/* caller expects it to be NULL after alleged unmapping */
}

struct page *alias_vmap_to_page(void *p)
{
	struct page *page;
	page = vmalloc_to_page(p);
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias =
		page_ext_data(page_ext, &page_alias_ops);
	while(atomic_cmpxchg(&page_alias->do_not_move, -1, -1));
	atomic_inc(&page_alias->do_not_move);
	page_ext_put(page_ext);
	return page;
}

void alias_page_close(struct page *page)
{
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias =
		page_ext_data(page_ext, &page_alias_ops);
	BUG_ON(atomic_read(&page_alias->do_not_move) <= 0);
	atomic_dec(&page_alias->do_not_move);
	page_ext_put(page_ext);
	put_page(page);
}

// need to add support for list, now for one, also we didnt add an option to remove from the rmap yet.
// void add_to_alias_rmap(struct page *page, void *ptr)
// {
// 	struct page_ext *page_ext = page_ext_get(page);
// 	struct page_alias *page_alias =
// 		page_ext_data(page_ext, &page_alias_ops);
	
// 	page_alias->iommu_rmap_list.curr = ptr;
// 	int prev_count = atomic_read(&(page_alias->kernel_ref_count));
// 	if (prev_count)
// 		atomic_inc(&(
// 			page_alias->kernel_ref_count)); //all of this needs to be atomic, for now like this to save time before deciding how.
// 	else
// 		atomic_set(&page_alias->kernel_ref_count, 1);
// 	page_ext_put(page_ext);
// }

int get_alias_refcount(struct page *page)
{
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias =
		page_ext_data(page_ext, &page_alias_ops);
	int i = atomic_read(&(
		page_alias->kernel_ref_count)); //need to be atomic, for now returning int because in migrate expected ref count is an int...
	page_ext_put(page_ext);
	pr_info("refcount: %d", i);
	return i;
}

int is_alias_rmap_empty(struct page *page)
{
	/* returns 0 if empty, else 1 */
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias =
		page_ext_data(page_ext, &page_alias_ops);
	//struct rmap_alias r = page_alias->iommu_rmap_list;
	int i = (page_alias->iommu_rmap_list ? 1 : 0);
	page_ext_put(page_ext);
	return i;
}

void *get_alias_rmap(struct page *page)
{
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias =
		page_ext_data(page_ext, &page_alias_ops);
	page_ext_put(page_ext);
	return page_alias->iommu_rmap_list;
}

int start_pinned_migration(struct page *page){
	int ret;
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias =
		page_ext_data(page_ext, &page_alias_ops);
	ret = atomic_cmpxchg(&page_alias->do_not_move, 0, -1);
	page_ext_put(page_ext);
	return ret;
}

void end_pinned_migration(struct page *page){
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias =
		page_ext_data(page_ext, &page_alias_ops);
	atomic_set(&page_alias->do_not_move, 0);
	page_ext_put(page_ext);
}