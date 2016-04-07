#include <types.h>
#include <mips/types.h>
#include <vm.h>
#include <mips/vm.h>
#include <addrspace.h>
#include <array.h>

paddr_t coremap;
/* Format for entry in coremap
 * high 20 bits for virtual Address
 * next 6 bits unused
 * low 6 bits for address space identifier
 */

static void coremap_setProcessId(unsigned int physicalPageNumber, int processId) {

	((paddr_t *)&coremap)[physicalPageNumber] = (((paddr_t *)&coremap)[physicalPageNumber] & 0xFFFFFFC0)
			| (0x3F & processId);

}

static void coremap_setVirtualAddress(unsigned int physicalPageNumber,
		int virtualAddress) {

	((paddr_t *)&coremap)[physicalPageNumber] = (((paddr_t *)&coremap)[physicalPageNumber] & 0xFFF)
			| (virtualAddress << 12);

}

unsigned int totalPageCount;

void vm_bootstrap() {
	// TODO create core map here



	totalPageCount = ((unsigned int) ram_getsize()) / PAGE_SIZE;

	// calculate size of coremap
	unsigned int coremapPageCount = totalPageCount * 2 / PAGE_SIZE;


	// steal pages for coremap
	coremap = ram_stealmem(coremapPageCount);

	totalPageCount = (ram_getsize() - (coremap + coremapPageCount * PAGE_SIZE)) / PAGE_SIZE;

	// steal remaining pages from ram
	paddr_t pageAddresses = ram_stealmem(totalPageCount);
	//pageAddresses /= PAGE_SIZE;

	// initialize coremap
	unsigned int i;
	for (i = 0; i < totalPageCount; i++) {
		*((paddr_t *)coremap + i) = 0;
//		paddr_t mapptr = *((paddr_t *)coremap) + i*(sizeof(paddr_t));
//		(paddr_t*)*mapptr = (pageAddresses + i * PAGE_SIZE);
	}

	ram_getfirstfree();

}

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress) {
	// TODO implement this?
	(void) faulttype;
	(void) faultaddress;
	return 0;
}

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(unsigned npages) {

	unsigned int i = 0, j = 0;
	for (i = 0; i < totalPageCount; i++) {
		if ((((paddr_t *)&coremap)[i] & 0x3F) == 0) {
			for (j = i; j - i < npages; j++) {
				if ((((paddr_t *)&coremap)[j] & 0x3F) != 0) {
					break;
				}
			}
			if (j - i == npages) {
				unsigned int k = i;
				for (; k < j; k++) {
					coremap_setProcessId(k, 1);
					coremap_setVirtualAddress(k, k + 0x80000000);
				}
				return k + 0x80000000;
			}
		}
	}
//update coremap
	return 0;
// TODO implement this
}

void free_kpages(vaddr_t addr) {
//TODO implement this
	unsigned int i;
	for (i = 0; i < totalPageCount; i++) {
		if (((paddr_t *)&coremap)[i] / 4096 == addr) {
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
unsigned int coremap_used_bytes() {
	return 0;
// TODO implement this with some ugly sizeof's
}

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown_all() {
// TODO implement this. but what is this?
}

void vm_tlbshootdown(const struct tlbshootdown * tlb) {
	(void) tlb;
//TODO implement this. but what is this?
}
