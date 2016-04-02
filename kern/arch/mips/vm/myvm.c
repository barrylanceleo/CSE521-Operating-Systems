#include <vm.h>
#include <array.h>
#include "ram.c"

paddr_t coremap;
/* Format for entry in coremap
 * high 20 bits for virtual Address
 * next 6 bits unused
 * low 6 bits for address space identifier
 */

static void coremap_setProcessId(unsigned int physicalPageNumber, int processId) {

	(unsigned int*) coremap[physicalPageNumber] = (coremap[physicalPageNumber]
			& 0xFFFFFFC0) | (0x3F & processId);

}

static void coremap_setVirtualAddress(unsigned int physicalPageNumber,
		int virtualAddress) {

	coremap[physicalPageNumber] = (coremap[physicalPageNumber] & 0xFFF)
			| (virtualAddress << 12);

}

unsigned int totalPageCount;

void vm_bootstrap() {
	// TODO create core map here
	totalPageCount = ((int) ram_getsize()) / PAGE_SIZE;

	// calculate size of coremap
	unsigned int coremapPageCount = totalPageCount * 2 / PAGE_SIZE;

	totalPageCount -= coremapPageCount;
	// steal pages for coremap
	paddr_t coremap = ram_stealmem(coremapPageCount);

	// steal remaining pages from ram
	paddr_t pageAddresses = ram_stealmem(totalPageCount);
	pageAddresses /= PAGE_SIZE;

	// initialize coremap
	unsigned int i;
	for (i = 0; i < totalPageCount; i++) {
		(unsigned int*) *(coremap + i) = (pageAddresses + i);
	}

}

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress) {
	// TODO implement this?
	return 0;
}

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(unsigned npages) {

	int i = 0, j = 0;
	for (i = 0; i < totalPageCount; i++) {
		if (coremap[i] & 0x3F == 0) {
			for (j = i; j - i < npages; j++) {
				if (coremap[j] & 0x3F != 0) {
					break;
				}
			}
			if (j - i == npages) {
				int k = i;
				for (; k < j; k++) {
					coremap_setProcessId(k, 1);
					coremap_setVirtualAddress(k, k + 0x80000000);
				}
				return k + 0x80000000;
			}
		}
	}
//update coremap
	return NULL;
// TODO implement this
}

void free_kpages(vaddr_t addr) {
//TODO implement this
	int i;
	for (i = 0; i < totalPageCount; i++) {
		if(coremap[i] /4096 == addr) {
			coremap_setProcessId(i, 0);
			return;
		}
	}
	panic("FAILED");
// update core map
}

/*
 * Return amount of memory (in bytes) used by allocated coremap pages.  If
 * there are ongoing allocations, this value could change after it is returned
 * to the caller. But it should have been correct at some point in time.
 */
unsigned int coremap_used_bytes(void) {
	return 0;
// TODO implement this with some ugly sizeof's
}

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown_all(void) {
// TODO implement this. but what is this?
}

void vm_tlbshootdown(const struct tlbshootdown *) {
//TODO implement this. but what is this?
}
