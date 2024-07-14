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

struct iommu_rmap {
	struct iommu_domain *domain;
	unsigned long phys_pfn;
};

struct iommu_rmap empty_rmap = {
	.domain = NULL,
	.phys_pfn = 0
//.refcount = ATOMIC_INIT(0)
};

int iommu_rmap_empty(struct iommu_rmap a){
	return !(a.domain);
}

// #define for_each_alias_rmap(rmap, alias) 
//     for (rmap = alias->iommu_rmap_list; rmap->curr; rmap = rmap->next)
struct page_alias {
	atomic_t do_not_move; 
	/* -2: the page was not aliased before
	 * -1: the page is going through a migration
	 * 0: ok to move
	 * 1+: counter of places that currently hold the page struct
	 */
	atomic_t kernel_ref_count;
	atomic_t iommu_ref_count;
	void* kernel_rmap;
	struct iommu_rmap iommu_rmap;
	// bit_who_is_the_owner
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
	// atomic_set(&page_alias->do_not_move, 0);
	atomic_set(&page_alias->do_not_move, -2);
	atomic_set(&page_alias->kernel_ref_count, 0);
	atomic_set(&page_alias->iommu_ref_count, 0);
	page_alias->iommu_rmap = empty_rmap;
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


void alias_iommu_create_rmap(struct iommu_domain *domain, unsigned long phys_pfn) {
	/*
	 * create an iommu rmap for a single page if it 
	 * doesn't already exist
	 */
	// pr_info("in function %s", __func__);
	struct iommu_rmap new_rmap = {.domain = domain, .phys_pfn = phys_pfn};
	struct page *page = pfn_to_page(phys_pfn);
	// pr_info("the page in alias_iommu_create_rmap in = %ld\n", (unsigned long)&page);
	BUG_ON(!page);
	struct page_ext *page_ext = page_ext_get(page);
	if (!page_ext){
		pr_info("In function %s, page_ext is null,PFN=%lx,  but everything is chill\n", __func__,phys_pfn);
		return;
	}
	struct page_alias *page_alias = page_ext_data(page_ext, &page_alias_ops);
	struct iommu_rmap old_rmap = page_alias->iommu_rmap;
	// pr_info("shutaf1!");
	// pr_info("is page alias null : %d\n", page_alias==NULL);
	if(!page_alias){
		set_page_alias(page);
		page_ext = page_ext_get(page);
		page_alias = page_ext_data(page_ext, &page_alias_ops);
	}
	// pr_info("is page alias null : %d\n", page_alias==NULL);
	// pr_info("is page ext null : %d\n", page_ext==NULL);


		
	if(!atomic_inc_not_zero(&page_alias->iommu_ref_count)){
		// first time (?)
		// pr_info("shutaf2");
		cmpxchg((unsigned long *)&(page_alias->iommu_rmap), *(unsigned long *)(&old_rmap), *(unsigned long *)(&new_rmap));
	// 	pr_info("shutaf3");
		atomic_inc(&page_alias->iommu_ref_count);
	// 	pr_info("shutaf4");
	}
	page_ext_put(page_ext);
}

void alias_iommu_remove_rmap(unsigned long phys_pfn) {
	/* 
	 * remove an iommu rmap for a single page 
	 */
	struct page *page = pfn_to_page(phys_pfn);
	BUG_ON(!page);
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias = page_ext_data(page_ext, &page_alias_ops);
	if (!atomic_add_unless(&page_alias->iommu_ref_count, -1, 1)){
		if(atomic_cmpxchg(&page_alias->iommu_ref_count, 1, 0))
			page_alias->iommu_rmap = empty_rmap;
		else
			atomic_dec(&page_alias->iommu_ref_count);
	}
	page_ext_put(page_ext); 
}



void *alias_vmap(struct page *page)
{
	/* create a kernel alias for a single page if it 
	 * doesn't already exist and return a 
	 * pointer to its vmap
	 */
	void *vmap_address, *old_rmap;
	BUG_ON(!page);
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias =
		page_ext_data(page_ext, &page_alias_ops);
	vmap_address = page_alias->kernel_rmap;
	old_rmap = page_alias->kernel_rmap;
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
		pr_info("Is page alias null? %d\n", (page_alias==NULL));
		pr_info("Is page alias's kernel rmap null? %d\n", (page_alias->kernel_rmap==NULL));
		if (cmpxchg(&page_alias->kernel_rmap, old_rmap, vmap_address)){
		/* someone else created the vmap before us */
			pr_info("was null but created before me!\n");
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
	atomic_cmpxchg(&page_alias->do_not_move, -2, 0);//If this is it's first vmap ever, allow moving it. (maybe better in the else above)
	page_ext_put(page_ext);
	BUG_ON(!is_vmalloc_addr(vmap_address));
	return vmap_address;
}

void alias_vunmap(void *p)
{
	BUG_ON(!is_vmalloc_addr(p));
	struct page* page = alias_vmap_to_page(p);
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias =
		page_ext_data(page_ext, &page_alias_ops);
	if (!atomic_add_unless(&page_alias->kernel_ref_count, -1, 1)) {
		if (atomic_cmpxchg(&page_alias->kernel_ref_count, 1, 0)) {
			page_ext_put(page_ext); /* because vmap might sleep */
			vunmap(p);
		} else {
		/* Either someone else unmapped, or someone else added a referance. */
			atomic_dec(&page_alias->kernel_ref_count);
			page_ext_put(page_ext);
		}
	}
	alias_page_close(page);
	p = NULL;/* caller expects it to be NULL after alleged unmapping */
}

struct page *alias_vmap_to_page(void *p)
{
	struct page *page;
	page = vmalloc_to_page(p);
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias =
		page_ext_data(page_ext, &page_alias_ops);//here
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

int get_alias_refcount(struct page *page)
{
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias =
		page_ext_data(page_ext, &page_alias_ops);
	int i = atomic_read(&(page_alias->kernel_ref_count)); //need to be atomic, for now returning int because in migrate expected ref count is an int...
	int j = atomic_read(&(page_alias->iommu_ref_count)); //need to be atomic, for now returning int because in migrate expected ref count is an int...
	page_ext_put(page_ext);
	pr_info("kernel refcounts: %d, iommu: %d", i, j);
	//TODO: what about iommu references? maybe our iommu reference doesnt count because we hold the physicak address and not a pointer to the virtual one.
	// so no need to return + j
	return i;//
}

int is_alias_rmap_empty(struct page *page)
{
	/* returns 1 if empty, else 0 */
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias =
		page_ext_data(page_ext, &page_alias_ops);
	int ret = (iommu_rmap_empty(page_alias->iommu_rmap) && !(page_alias->kernel_rmap));
	page_ext_put(page_ext);
	return ret;
}

int is_alias_dma_page(struct page *page){
	/* returns 1 if dma, else 0 */
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias =
		page_ext_data(page_ext, &page_alias_ops);
	int ret = !iommu_rmap_empty(page_alias->iommu_rmap);
	page_ext_put(page_ext);
	return ret;
}



int is_alias_kernel_page(struct page *page){
	/* returns 1 if kernel pinned, else 0 */
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias =
		page_ext_data(page_ext, &page_alias_ops);
	int ret = !!(page_alias->kernel_rmap);
	page_ext_put(page_ext);
	return ret;
}

void *get_alias_rmap(struct page *page)
{
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias =
		page_ext_data(page_ext, &page_alias_ops);
	void* ret;
	int i = atomic_read(&(page_alias->iommu_ref_count)); //for debug, remove later
	if((page_alias->kernel_rmap) && (i)){
		pr_info("both kernel and iommu rmaps!\n");
	}
	if (page_alias->kernel_rmap){
		pr_info("kernel_rmap\n");
		ret = page_alias->kernel_rmap;
	}
	else{ //iommu
		pr_info("iommu_rmap\n");
		//pfn_to_dma_pte
		ret = phys_to_virt(page_alias->iommu_rmap.phys_pfn); //not sure if this is good, because it's a kernel's address
	}
	page_ext_put(page_ext);
	return ret;
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

void call_dma_migrate_page(struct page *page, bool prepare, struct folio *folio){
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias = page_ext_data(page_ext, &page_alias_ops);
	struct iommu_rmap iommu_rmap = page_alias->iommu_rmap;
	struct iommu_domain* domain = iommu_rmap.domain;
	domain->ops->migrate_page(domain, iommu_rmap.phys_pfn, folio, prepare);
	page_ext_put(page_ext);
}