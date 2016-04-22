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

// core map data structure
struct core_map_entry* coremap;

// lock for the coremap data structure
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;

// starting address of first physical page
unsigned first_paddr;

// number of pages excluding the coremap
unsigned page_count;

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

	//kprintf("Start of coremap: %u\n", first_paddr);
	coremap = (struct core_map_entry*) PADDR_TO_KVADDR(first_paddr);
	first_paddr += coremap_size;
	//kprintf("End of coremap: %u\n", first_paddr);

	// align the pages, the first page should start with an address which is a multiple of PAGE_SIZE
	if (first_paddr % PAGE_SIZE != 0) {
		first_paddr += PAGE_SIZE - (first_paddr % PAGE_SIZE);
		//kprintf("CoreMap Size: %u bytes = %u pages\n", coremap_size,
		//coremap_size / PAGE_SIZE + 1);
	} else {
		//kprintf("CoreMap Size: %u bytes = %u pages\n", coremap_size,
		//coremap_size / PAGE_SIZE);
	}

	// update the page count, may reduce due to space allocation for coremap
	page_count = (last_paddr - first_paddr) / PAGE_SIZE;
	//kprintf("Pages available for use: %u\n", page_count);

	//kprintf("Start of usable memory: %u\n", first_paddr);
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

static int isWithinStack(struct addrspace* as, vaddr_t faultaddress) {
	return (USERSTACK - faultaddress) / PAGE_SIZE <= as->as_stackPageCount + 1;
}

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress) {
	// TODO implement this?
	struct addrspace* as = proc_getas();
	if (as == NULL) {
		return EFAULT;
	}
	struct region* reg = findRegionForFaultAddress(as, faultaddress);
	if (reg == NULL) {
		if (!isWithinStack(as, faultaddress)) {
			//kprintf("VMFault returned ENOSYS for type = %d, address = %x\n", faulttype, faultaddress);
			return EFAULT;
		} // TODO fill this

	}
	// TODO Check if it is a permission issue and return an error code in that case.

	// get page
	struct page* pg = findPageForFaultAddress(as, faultaddress);
	if (pg == NULL) {
		struct page* newpage = page_create(as, faultaddress);
		pg = newpage;
		if (isWithinStack(as, faultaddress)) {
			kprintf("Created a new stack page %d ,%u\n", as->as_stackPageCount,array_num(as->as_pagetable));
			as->as_stackPageCount++;
		}
	}
	if(pg == NULL) {
		return EFAULT;
	}

	// load page address to tlb
	/*kprintf("VMFault Writign to tlb type = %d, address = %x\n", faulttype,
	 faultaddress);*/
	int spl = splhigh();
	tlb_random(pg->pt_virtbase * PAGE_SIZE,
			(pg->pt_pagebase * PAGE_SIZE) | TLBLO_DIRTY | TLBLO_VALID);
	splx(spl);
	(void) faulttype;
	return 0;
}

vaddr_t coremap_allocuserpages(unsigned npages, struct addrspace * as) {
	spinlock_acquire(&coremap_lock);
	unsigned i = 0, j = 0;
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
				spinlock_release(&coremap_lock);
				bzero(PADDR_TO_KVADDR((void* )output_paddr),
						npages * PAGE_SIZE);
				return output_paddr;
			}
		}
	}
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
				j++;
			}
			spinlock_release(&coremap_lock);
			return;
		}
	}
	spinlock_release(&coremap_lock);
	panic("free_kpages() failed, did not find the given vaddr");
	// to remove the function not used erro
	(void) cm_getEntryAddrspaceIdent((struct core_map_entry *) (coremap));
	(void) cm_isEntryDirty((struct core_map_entry *) (coremap));
}

void free_kpages(vaddr_t addr) {
	coremap_freeuserpages(addr - MIPS_KSEG0);
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
			coremap_freeuserpages(pageCandidate->pt_pagebase * PAGE_SIZE);
			kfree(pageCandidate);
			i--;
			pageCount = array_num(as->as_pagetable);
		}
	}
}

static void invalidateTlb() {
	int spl = splhigh();
	int i;
	for (i = 0; i < NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	splx(spl);
}

static int reduceHeapSize(userptr_t amount, int32_t* retval,
		struct addrspace* as) {
	//kprintf("AMOUNT : %d, Current Pointer = %x\n", (int) amount,
	//		as->as_addrPtr);
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
		if (reg != NULL && reg->rg_vaddr >= newStart) {
			removePagesWithinRegion(as, reg);
			array_remove(as->as_regions, i);
			kfree(reg);
			i--;
			regionCount = array_num(as->as_regions);
		} else if (reg != NULL && reg->rg_vaddr <= newStart && reg->rg_vaddr  + reg->rg_size >= newStart) {
			struct region tempReg;
			tempReg.rg_vaddr = newStart;
			tempReg.rg_size = newStart - (reg->rg_vaddr + reg->rg_size);
			reg->rg_size = newStart - reg->rg_vaddr;
			removePagesWithinRegion(as, &tempReg);
		}
	}
	invalidateTlb();
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
	if((int)amount > 1024 *256*PAGE_SIZE) {
		*retval = -1;
		return ENOMEM;
	}
	struct addrspace* as = proc_getas();
	if(as->as_heapBase == 0) {
		as->as_heapBase= as->as_addrPtr;
	}
	if (amount == 0) {
		*retval = as->as_addrPtr;
		return 0;
	}

	if ((int) amount < 0) {
		if((int)(amount + as->as_addrPtr) <= (int)(as->as_heapBase)){
			*retval = -1;
			return EINVAL;
		}
		return reduceHeapSize(amount, retval, as);

	}

	vaddr_t newRegionStart = as->as_addrPtr;
	/*kprintf("SBRK CALLED WITH PARAMS %x, newregion start = %x\n", (int) amount,
			newRegionStart);*/
	as_define_region(as, newRegionStart, (int) amount, 1, 1, 0);

	*retval = newRegionStart;
	return 0;
}
