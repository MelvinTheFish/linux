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
struct rmap_alias{
        struct rmap_alias* next;
        void* curr;
};

struct page_alias{
        int do_not_move: 1;
        refcount_t ref_count;
        struct rmap_alias rmap_list;
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
        page_alias->do_not_move = 0;
        refcount_set(&page_alias->ref_count, 0);
	//printk(KERN_ERR "omer and nizan: in set __set_page_ext_alias");
        page_alias->rmap_list.curr = NULL;
        page_alias->rmap_list.next = NULL;
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


void* alias_vmap(struct page **pages, int n){
	void* vmap_address = vmap(pages, n, VM_MAP, PAGE_KERNEL); 
	return vmap_address;
}


void alias_vunmap(void* p){
  vunmap(p); 
	p = NULL;
}

struct page* alias_vmap_to_page(void* p){
	struct page* page;
	
	page = vmalloc_to_page(p);
	
	struct page_ext* page_ext = page_ext_get(page); 
	struct page_alias* page_alias = page_ext_data(page_ext, &page_alias_ops);
  page_alias->do_not_move = 1;
	page_ext_put(page_ext);
	return page;
}

void alias_page_close(struct page* page)
{
	struct page_ext* page_ext = page_ext_get(page); 
	struct page_alias* page_alias = page_ext_data(page_ext, &page_alias_ops);
  page_alias->do_not_move = 0;
	page_ext_put(page_ext);

	put_page(page);
}


// need to add support for list, now for one
void add_to_alias_rmap(struct page* page, void* ptr){
	struct page_ext* page_ext = page_ext_get(page); 
	struct page_alias* page_alias = page_ext_data(page_ext, &page_alias_ops);
  page_alias->rmap_list.curr = ptr;
	page_ext_put(page_ext);
}

int is_alias_rmap_empty(struct page* page){
	//returns 0 if empty, else 1
	struct page_ext* page_ext = page_ext_get(page); 
	struct page_alias* page_alias = page_ext_data(page_ext, &page_alias_ops);
        //struct rmap_alias r = page_alias->rmap_list;
	int i = (page_alias->rmap_list.curr ? 1 : 0);
	page_ext_put(page_ext);
	return i;
}


void check_migration_at_start(struct list_head *from){
	// Iterate over each entry in the page list and print if it's rmap is empty
	// struct folio* folio;
	// struct page* page;
	// int cnt = 1;
	// printk(KERN_INFO "Omer and Nizan: In check_migration_at_start, starting to iterate");
 //    	list_for_each_entry(folio, from, lru) {
	// 	page = &folio->page;
	// 	if (!page)
	// 	{
	// 		printk(KERN_INFO "Omer and Nizan: Folio: %d's page is null", cnt);
	// 	}else{
	// 		if(is_alias_rmap_empty(page) == 0)
	// 			printk(KERN_INFO "Page: %d's rmap is empty (OI LI! :0) ", cnt);
	// 		else
	// 			printk(KERN_INFO "Page: %d's rmap is not empty (OH YES :)", cnt);
	// 		cnt++;
	// 	}	
	// }
	// printk(KERN_INFO "Omer and Nizan: In check_migration_at_start, finished to iterate");

}



