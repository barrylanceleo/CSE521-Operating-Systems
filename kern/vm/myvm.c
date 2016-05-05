#include <types.h>
#include <mips/types.h>
#include <vm.h>
#include <mips/vm.h>
#include <addrspace.h>
#include <array.h>
#include <synch.h>
#include <proc.h>
#include <current.h>
#include <kern/errno.h>
#include <mips/tlb.h>
#include <syscall.h>
#include <spl.h>
#include <kern/fcntl.h>
#include <vnode.h>
#include <vfs.h>
#include <stat.h>
#include <uio.h>
#include <current.h>

// core map data structure
struct core_map_entry* coremap;

// lock for the coremap data structure
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;

// starting address of first physical page
unsigned first_paddr;

// number of pages excluding the coremap
unsigned page_count;

unsigned coremap_pages_free;

#define COREMAP(i) (struct core_map_entry *)(coremap + i)

static void cm_setEntryAddrspaceIdent(struct core_map_entry *entry,
		struct addrspace * as) {

	entry->as = as;
}

static void cm_setEntryChunkStart(struct core_map_entry *entry,
		unsigned chunk_start) {

	entry->chunk_start = chunk_start;
}

static paddr_t cm_getEntryPaddr(unsigned page_index) {

	return first_paddr + page_index * PAGE_SIZE;
}

static struct addrspace * cm_getEntryAddrspaceIdent(
		struct core_map_entry *entry) {

	return entry->as;
}

static unsigned cm_getEntryChunkStart(struct core_map_entry *entry) {

	return entry->chunk_start;
}

static bool cm_isEntryUsed(struct core_map_entry *entry) {

	// check the page_state variable
	if ((entry->page_state & 0x01) > 0) {
		return true;
	} else {
		return false;
	}
}

static bool cm_isEntryDirty(struct core_map_entry *entry) {

	// check the page_state variable
	if ((entry->page_state & 0x02) > 0) {
		return true;
	} else {
		return false;
	}
}

static void cm_setEntryUseState(struct core_map_entry *entry, bool state) {

	// update the page_state variable
	if (state) {
		entry->page_state |= 0x01;
	} else {
		entry->page_state &= ~0x01;
	}
}

static void cm_setEntryDirtyState(struct core_map_entry *entry, bool state) {

	// update the page_state variable
	if (state) {
		entry->page_state |= 0x02;
	} else {
		entry->page_state &= ~0x02;
	}
}

void vm_bootstrap() {

	// get the number of free pages in ram
	paddr_t last_paddr = ram_getsize();
	first_paddr = ram_getfirstfree();

	// calculate the total page_count and coremap size
	page_count = (last_paddr - first_paddr) / PAGE_SIZE; // discarding the last addresses, if any
	size_t coremap_size = (sizeof(struct core_map_entry) * page_count);

	// can't call ram_stealmem() after calling ram_getfirstfree,
	// so steal memory for coremap here and update first_paddr
	if (first_paddr + coremap_size > last_paddr) {
		panic("Unable to allocate space for coremap");
	}

	coremap = (struct core_map_entry*) PADDR_TO_KVADDR(first_paddr);
	first_paddr += coremap_size;

	// align the pages, the first page should start with an address which is a multiple of PAGE_SIZE
	if (first_paddr % PAGE_SIZE != 0) {
		first_paddr += PAGE_SIZE - (first_paddr % PAGE_SIZE);
	} else {
		//kprintf("CoreMap Size: %u bytes = %u pages\n", coremap_size,
		//coremap_size / PAGE_SIZE);
	}

	// update the page count, may reduce due to space allocation for coremap
	page_count = (last_paddr - first_paddr) / PAGE_SIZE;

	coremap_pages_free = page_count;

	// initialize the coremap
	unsigned i;
	for (i = 0; i < page_count; i++) {

		// update the state
		cm_setEntryUseState(COREMAP(i), false);
		cm_setEntryDirtyState(COREMAP(i), false);

		// let the address space identifier be NULL initially
		cm_setEntryAddrspaceIdent(COREMAP(i), NULL);

		// initial chunk start need not be initialized, will be updated when page is allocated
	}

}

static struct region* findRegionForFaultAddress(struct addrspace* as,
		vaddr_t address) {
	int regionCount = array_num(as->as_regions);
	int i;
	for (i = 0; i < regionCount; i++) {
		struct region* reg = array_get(as->as_regions, i);
		if (reg != NULL && reg->rg_vaddr <= address
				&& (reg->rg_vaddr + reg->rg_size) > address) {
			return reg;
		}
	}
	//kprintf("Region count is : %u\n",regionCount);
	return NULL;
}

static struct page* findPageForFaultAddress(struct addrspace* as,
		vaddr_t faultaddress) {
	int pageCount = array_num(as->as_pagetable);
	int i;
	for (i = 0; i < pageCount; i++) {
		struct page *pageCandidate = array_get(as->as_pagetable, i);
		if (pageCandidate != NULL
				&& pageCandidate->pt_virtbase == faultaddress / PAGE_SIZE) {
			return pageCandidate;
		}
	}
	return NULL;
}

#define SWAP_STATE_UNINIT 0
#define SWAP_STATE_NOSWAP -1
#define SWAP_STATE_READY 1

#define SWAP_PAGE_FREE 0
#define SWAP_PAGE_USED 1

struct swap_entry {
	int se_used;
	int se_paddr;
};

static int swap_state = 0;

static struct swap_entry* swap_map = NULL;

static int swap_page_count;

static struct vnode* swap_vnode;

static struct lock* swap_lock;

#define SWAP_DISK_NAME "lhd0raw:"

void swap_init() {
	int ret = vfs_open((char*) SWAP_DISK_NAME, O_RDWR, 0, &swap_vnode);
	if (ret) {
		kprintf("WARN swap disk not found Ret = %d\n", ret);
		swap_state = SWAP_STATE_NOSWAP;
		return;
	}
	struct stat statbuf;
	ret = VOP_STAT(swap_vnode, &statbuf);
	if (ret) {
		kprintf("ERR Stat on swap disk failed\n");
		swap_state = SWAP_STATE_NOSWAP;
		return;
	}

	swap_page_count = (statbuf.st_size / PAGE_SIZE) - 1;
	swap_map = (struct swap_entry*) kmalloc(
			sizeof(struct swap_entry) * swap_page_count);

	int i;
	for (i = 0; i < swap_page_count; i++) {
		swap_map[i].se_used = SWAP_PAGE_FREE;
		swap_map[i].se_paddr = i;
	}

	swap_lock = lock_create("swap_lock");

	swap_state = SWAP_STATE_READY;
	kprintf("Swap init done. Total available pages = %d\n", swap_page_count);

}

int swap_prev_write_idx = 0;

#define SWAP_IDX(idx) (((int)idx >= (int)page_count ? (int)idx - (int)page_count : (int)idx))
#define COREMAP_SWAP(i) COREMAP(SWAP_IDX(i + swap_prev_write_idx))

static int getOneSwapPage() {
	int i = 0;
	for (i = 0; i < swap_page_count; i++) {
		if (swap_map[i].se_used == SWAP_PAGE_FREE) {
			swap_map[i].se_used = 1;
			return i;
		}
	}
	return -1;
}

static struct page* findPageFromCoreMap(struct core_map_entry* cm, int idx) {
	int num = array_num(cm->as->as_pagetable);
	int i;
	for (i = 0; i < num; i++) {
		struct page* pg = array_get(cm->as->as_pagetable, i);
		if (pg->pt_pagebase == cm_getEntryPaddr(idx) / PAGE_SIZE) {
			return pg;
		}
	}
	panic("couldnt find page in address space\n");
	return NULL;

}

static void swaponepageout(struct page* pg, paddr_t phyaddr) {
	int swapPageindex = pg->pt_pagebase;
	struct iovec iov;
	struct uio kuio;
	iov.iov_kbase = (void*) PADDR_TO_KVADDR(phyaddr);
	iov.iov_len = PAGE_SIZE; // length of the memory space
	kuio.uio_iov = &iov;
	kuio.uio_iovcnt = 1;
	kuio.uio_resid = PAGE_SIZE; // amount to write to the file
	kuio.uio_space = NULL;
	kuio.uio_offset = swap_map[swapPageindex].se_paddr * PAGE_SIZE;
	kuio.uio_segflg = UIO_SYSSPACE;
	kuio.uio_rw = UIO_READ;
	// 4. write them to disk
	int result = VOP_READ(swap_vnode, &kuio);
	if (result) {
		// release lock on the vnode
		panic("READ FAILED!\n");
		return;
	}

	swap_map[swapPageindex].se_used = SWAP_PAGE_FREE;
	kprintf("Swap out:\tswap= %x,\tpage=%x \n",swapPageindex,pg->pt_virtbase);
	pg->pt_state = PT_STATE_MAPPED;
	pg->pt_pagebase = phyaddr / PAGE_SIZE;


}

static void swaponepagein(int idx, struct addrspace* as) {
	(void) as;
	// 3. clear their tlb entries if present TODO
	struct page* pg = findPageFromCoreMap(COREMAP(idx), idx);
	int spl = splhigh();
	int tlbpos = tlb_probe(pg->pt_pagebase * PAGE_SIZE, 0);
	if (tlbpos >= 0) {
		tlb_write(TLBHI_INVALID(tlbpos), TLBLO_INVALID(), tlbpos);
	} else {
		//kprintf("was not on tlb\n");
	}
	splx(spl);

	int swapPageindex = getOneSwapPage();
	kprintf("Swap in :\tswap= %x,\tpage=%x \n",swapPageindex,pg->pt_virtbase);
	//kprintf("Swap in page Vaddr = %x\n", pg->pt_virtbase);
	struct iovec iov;
	struct uio kuio;
	iov.iov_kbase = (void*) PADDR_TO_KVADDR(cm_getEntryPaddr(idx));
	iov.iov_len = PAGE_SIZE; // length of the memory space
	kuio.uio_iov = &iov;
	kuio.uio_iovcnt = 1;
	kuio.uio_resid = PAGE_SIZE; // amount to write to the file
	kuio.uio_space = NULL;
	kuio.uio_offset = swap_map[swapPageindex].se_paddr * PAGE_SIZE;
	kuio.uio_segflg = UIO_SYSSPACE;
	kuio.uio_rw = UIO_WRITE;
	//kprintf("before write \n");
	// 4. write them to disk
	spinlock_release(&coremap_lock);
	int result = VOP_WRITE(swap_vnode, &kuio);
	spinlock_acquire(&coremap_lock);
	if (result) {
		// release lock on the vnode
		panic("WRITE FAILED!\n");
		return;
	}
	//kprintf("write complete\n");

	pg->pt_state = PT_STATE_SWAPPED;
	pg->pt_pagebase = swap_map[swapPageindex].se_paddr;
}

static void swapin(int npages, struct addrspace* as) {
	// 1. check if coremap lock is already held, else acquire it

	if (swap_state == SWAP_STATE_NOSWAP) {
		panic("Attempting to swap in when no swap disk is found!\n");
	}
	//kprintf("HERerE1\n");
	lock_acquire(swap_lock);
	//spinlock_release(&coremap_lock);
	// 2. select a bunch of non kernel pages.
	//kprintf("Swap in of %d pages\n", npages);
	unsigned int i;

	for (i = 0; i < page_count; i++) {
		if (cm_isEntryUsed(
				COREMAP_SWAP(
						i)) && cm_getEntryAddrspaceIdent(COREMAP_SWAP(i)) != NULL ) {
			struct page* pg = NULL;
			if (cm_getEntryAddrspaceIdent(COREMAP_SWAP(i)) == as) {
				pg = findPageFromCoreMap(COREMAP_SWAP(i),
						SWAP_IDX(i + swap_prev_write_idx));
				int spl = splhigh();
				int tlbpos = tlb_probe(pg->pt_virtbase * PAGE_SIZE, 0);
				if (tlbpos >= 0) {
					splx(spl);
					//kprintf("tlb hit at %x %d\n",pg->pt_pagebase, tlbpos);
					continue;
				}
				splx(spl);
			}
			int j = 0;
			int flag = 1;
			for (j = 0; j < npages; j++) {
				if (i + j >= page_count) {
					kprintf("page count greater than i+j\n");
					flag = 0;
					break;
				}
				if (cm_isEntryUsed(
						COREMAP_SWAP(
								i + j)) && cm_getEntryAddrspaceIdent(COREMAP_SWAP(i + j)) == NULL) {
					kprintf("page used by kernel\n");
					flag = 0;
					break;
				}
			}
			if (flag == 1) {
				for (j = 0; j + i < page_count && j < npages; j++) {
					if (cm_isEntryUsed(
							COREMAP_SWAP(
									i + j)) && cm_getEntryAddrspaceIdent(COREMAP_SWAP(i + j)) != NULL) {
						swaponepagein(SWAP_IDX(i + j + swap_prev_write_idx), as);
						cm_setEntryUseState(COREMAP_SWAP(i + j), false);
						cm_setEntryDirtyState(COREMAP_SWAP(i + j), false);
						// let the address space identifier be NULL initially
						cm_setEntryAddrspaceIdent(COREMAP_SWAP(i + j), NULL);
						coremap_pages_free++;
						swap_prev_write_idx = SWAP_IDX(i + j+ swap_prev_write_idx) + 1;
						/*if(pg != NULL) {
							kprintf("swapped in page was = %x\n", pg->pt_virtbase);
						}*/
					} else {
						kprintf("How did this get approved?\n");
					}
				}
				//spinlock_acquire(&coremap_lock);
				lock_release(swap_lock);
				return;
			}
		}
	}
	panic("Out of pages to swap out!\n");
	//spinlock_acquire(&coremap_lock);
	lock_release(swap_lock);

	// 2.5 Maintain a index of last page that was swapped in so that you swap in the one after that

	// 5. free lock if you had acquired it in this method
}

static void swapout(struct addrspace*as, struct page* pg) {
	if (swap_state == SWAP_STATE_NOSWAP) {
		panic("Attempting to swap out when no swap disk is found!\n");
	}
	// 2. allocate a page in coremap for this
	paddr_t phaddress = coremap_allocuserpages(1, as);
	if (phaddress == 0) {
		kprintf("no page to swap in to \n");
		return;
	}

	// 3. copy content from disk to the new page
	// 4. update page table entry
	// 5. free the swap page
	swaponepageout(pg, phaddress);

}

static void swapfree(struct page* pg) {
	if (swap_state == SWAP_STATE_NOSWAP) {
		panic("Attempting to swap out when no swap disk is found!\n");
	}
	//kprintf("HERRE\n");
	lock_acquire(swap_lock);
	if (swap_state == SWAP_STATE_NOSWAP) {
		panic("Attempting to swap free when no swap disk is found!\n");
	}
	swap_map[pg->pt_pagebase].se_used = SWAP_PAGE_FREE;
	// 1. check if coremap lock is already held, else acquire it
	// 2. free the swap page. // TODO create a common method for freeing pages
	// 3. release lock if you had acquired it in this method
	lock_release(swap_lock);
}

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress) {
	struct addrspace* as = proc_getas();
	if (as == NULL) {
		//kprintf("AS was null\n");
		return EFAULT;
	}
	struct region* reg = findRegionForFaultAddress(as, faultaddress);
	if (reg == NULL) {
		//kprintf("Region not found\n");
		return EFAULT;
	}
	// TODO Check if it is a permission issue and return an error code in that case.

	// get page
	struct page* pg = findPageForFaultAddress(as, faultaddress);
	if (pg == NULL) {
		struct page* newpage = page_create(as, faultaddress);
		pg = newpage;
	}
	if (pg == NULL) {
		//kprintf("Failed to create a page\n");
		return EFAULT;
	}
	if (pg->pt_state == PT_STATE_SWAPPED) {
		//kprintf("Trying swap out from %x\n",pg->pt_pagebase);
		//kprintf("Swap out page Vaddr = %x\n",pg->pt_virtbase);
		swapout(as, pg);
		//kprintf("after swap out paddr = %x\n",pg->pt_pagebase);
		//kprintf("after Swap out Vaddr = %x\n", pg->pt_virtbase);
		//kprintf("after Swap out state = %d\n",pg->pt_state);
	}

	// load page address to tlb
	int spl = splhigh();
	tlb_random(pg->pt_virtbase * PAGE_SIZE,
			(pg->pt_pagebase * PAGE_SIZE) | TLBLO_DIRTY | TLBLO_VALID);
	splx(spl);
	(void) faulttype;
	return 0;
}

vaddr_t coremap_allocuserpages(unsigned npages, struct addrspace * as) {
	//kprintf("before inside cm alloc\n");
	spinlock_acquire(&coremap_lock);
	//kprintf("inside cm alloc\n");
	unsigned i = 0, j = 0;

	if (swap_state == SWAP_STATE_READY && coremap_pages_free == 0) {
		swapin(npages, as);
	}

	// search for n continuous free pages
	for (i = 0; i < page_count; i++) {
		if (!cm_isEntryUsed(COREMAP(i))) {
			for (j = i + 1; j < page_count && j - i < npages; j++) {
				if (cm_isEntryUsed(COREMAP(j))) {
					break;
				}
			}
			if (j - i == npages) {
				unsigned k = i;
				for (; k < j; k++) {
					// update the state
					cm_setEntryUseState(COREMAP(k), true);
					cm_setEntryDirtyState(COREMAP(k), true);
					// let the address space identifier be NULL for now
					cm_setEntryAddrspaceIdent(COREMAP(k), as);
					// chunk start would be the first page in the chunk
					cm_setEntryChunkStart(COREMAP(k), i);
				}
				paddr_t output_paddr = cm_getEntryPaddr(i);
				//kprintf("exiting cm alloc\n");
				spinlock_release(&coremap_lock);
				bzero(PADDR_TO_KVADDR((void* )output_paddr),
						npages * PAGE_SIZE);
				coremap_pages_free -= npages;
				return output_paddr;
			}
		}
	}
	//kprintf("could not allocate %u\n", npages);
	spinlock_release(&coremap_lock);
	return 0;
}

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(unsigned npages) {
	vaddr_t retval = coremap_allocuserpages(npages, NULL);
	return retval == 0 ? retval : PADDR_TO_KVADDR(retval);
}

void coremap_freeuserpages(paddr_t addr) {

	spinlock_acquire(&coremap_lock);
	unsigned i;
	for (i = 0; i < page_count; i++) {
		// find the coremap entry to free
		if (cm_getEntryPaddr(i) == addr) {
			// free all the pages in the chunk
			unsigned chunk_start = cm_getEntryChunkStart(COREMAP(i));
			unsigned j = i;
			while (j < page_count && cm_isEntryUsed(COREMAP(j))
					&& cm_getEntryChunkStart(COREMAP(j)) == chunk_start) {
				// update the state
				cm_setEntryUseState(COREMAP(j), false);
				cm_setEntryDirtyState(COREMAP(j), false);
				// let the address space identifier be NULL initially
				cm_setEntryAddrspaceIdent(COREMAP(j), NULL);
				coremap_pages_free++;
				j++;
			}
			spinlock_release(&coremap_lock);
			return;
		}
	}
	spinlock_release(&coremap_lock);
	panic("free_pages() failed, did not find the given vaddr\n");
	(void) swapfree(0);
	// to remove the function not used erro
	(void) cm_getEntryAddrspaceIdent((struct core_map_entry *) (coremap));
	(void) cm_isEntryDirty((struct core_map_entry *) (coremap));
}

void free_kpages(vaddr_t addr) {
	coremap_freeuserpages(addr - MIPS_KSEG0);
}

void freePage(struct page* page) {
	if(page->pt_state == PT_STATE_MAPPED) {
		coremap_freeuserpages(page->pt_pagebase * PAGE_SIZE);
		int spl = splhigh();
		int tlbpos = tlb_probe(page->pt_pagebase * PAGE_SIZE, 0);
		if (tlbpos >= 0) {
			tlb_write(TLBHI_INVALID(tlbpos), TLBLO_INVALID(), tlbpos);
		}
		splx(spl);
	} else if(page->pt_state == PT_STATE_MAPPED) {
		swapfree(page);
	}
}

/*
 * Return amount of memory (in bytes) used by allocated coremap pages.  If
 * there are ongoing allocations, this value could change after it is returned
 * to the caller. But it should have been correct at some point in time.
 */
unsigned int coremap_used_bytes() {
	spinlock_acquire(&coremap_lock);
	// traverse the coremap and find the number of allocated pages
	unsigned i, used_pages_count = 0;
	for (i = 0; i < page_count; i++) {
		if (cm_isEntryUsed(COREMAP(i))) {
			used_pages_count++;
		}
	}
	spinlock_release(&coremap_lock);
	return used_pages_count * PAGE_SIZE;
}

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown_all() {
// TODO implement this. but what is this?
}

void vm_tlbshootdown(const struct tlbshootdown * tlb) {
	(void) tlb;
//TODO implement this. but what is this?
}

static void removePagesWithinRegion(struct addrspace* as, struct region* reg) {
	int pageCount = array_num(as->as_pagetable);
	int i;
	for (i = 0; i < pageCount; i++) {
		struct page *pageCandidate = array_get(as->as_pagetable, i);
		if (pageCandidate != NULL
				&& pageCandidate->pt_virtbase >= reg->rg_vaddr / PAGE_SIZE
				&& pageCandidate->pt_virtbase
						<= (reg->rg_vaddr + reg->rg_size) / PAGE_SIZE) {
			array_remove(as->as_pagetable, i);
			freePage(pageCandidate);
			kfree(pageCandidate);
			i--;
			pageCount = array_num(as->as_pagetable);
		}
	}
}

/*static void invalidateTlb() {
	int spl = splhigh();
	int i;
	for (i = 0; i < NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	splx(spl);
}*/

static int reduceHeapSize(userptr_t amount, int32_t* retval,
		struct addrspace* as) {
	vaddr_t newStart = as->as_addrPtr + (vaddr_t) amount;
	vaddr_t oldStart = as->as_addrPtr;
	unsigned int regionCount = array_num(as->as_regions);
	unsigned int i;
	if (as->as_heapBase > newStart) {
		//kprintf("Invalid because it falls within code page %x\n",
		//		codepage->rg_vaddr + codepage->rg_size);
		*retval = -1;
		return ENOMEM;
	}
	for (i = 0; i < regionCount; i++) {
		struct region* reg = array_get(as->as_regions, i);
		if (reg != NULL && reg->rg_vaddr >= as->as_stackBase
				&& reg->rg_vaddr + reg->rg_size >= as->as_stackBase) {
			continue;
		}
		if (reg != NULL && reg->rg_vaddr >= newStart) {
			removePagesWithinRegion(as, reg);
			array_remove(as->as_regions, i);
			kfree(reg);
			i--;
			regionCount = array_num(as->as_regions);
		} else if (reg != NULL && reg->rg_vaddr <= newStart
				&& reg->rg_vaddr + reg->rg_size >= newStart) {
			struct region tempReg;
			tempReg.rg_vaddr = newStart;
			tempReg.rg_size = newStart - (reg->rg_vaddr + reg->rg_size);
			reg->rg_size = newStart - reg->rg_vaddr;
			removePagesWithinRegion(as, &tempReg);
		}
	}
	as->as_addrPtr = newStart;
	*retval = oldStart;
	//kprintf("retval is = %x\n", as->as_addrPtr);
	return 0;
}

int sys_sbrk(userptr_t amount, int32_t* retval) {
	*retval = 0;
	if ((int) amount % PAGE_SIZE != 0) {
		*retval = -1;
		return EINVAL;
	}
	if ((int) amount > 1024 * 256 * PAGE_SIZE) {
		*retval = -1;
		return ENOMEM;
	}
	struct addrspace* as = proc_getas();
	//kprintf("SBRK CALLED WITH PARAMS %x, newregion start = %x\n", (int) amount,
	//				as->as_addrPtr);
	if (as->as_heapBase == 0) {
		as->as_heapBase = as->as_addrPtr;
	}
	if (amount == 0) {
		*retval = as->as_addrPtr;
		return 0;
	}

	if ((int) amount < 0) {
		if ((int) (amount + as->as_addrPtr) < (int) (as->as_heapBase)) {
			*retval = -1;
			return EINVAL;
		}
		return reduceHeapSize(amount, retval, as);

	}

	vaddr_t newRegionStart = as->as_addrPtr;
	as_define_region(as, newRegionStart, (int) amount, 1, 1, 0);

	*retval = newRegionStart;
	return 0;
}
