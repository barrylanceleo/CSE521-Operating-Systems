/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <mips/tlb.h>
#include <spl.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

static unsigned int s_addrspaceCounter = 0;

static int as_getNewAddrSpaceId() {
	// TODO lock this up
	return s_addrspaceCounter++;
}

struct addrspace *
as_create(void) {
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}
	as->as_pagetable = array_create();
	as->as_regions = array_create();
	as->as_stackPageCount = 0;
	as->as_id = as_getNewAddrSpaceId();
	as->as_heapBase = 0;
	/*
	 * Initialize as needed.
	 */

	return as;
}

int as_copy(struct addrspace *old, struct addrspace **ret) {
	(void) old;

	struct addrspace *newas;
	newas = as_create();
	if (newas == NULL) {
		return ENOMEM;
	}
	newas->as_stackPageCount = old->as_stackPageCount;
	newas->as_addrPtr = old->as_addrPtr;
	newas->as_heapBase = old->as_heapBase;
	unsigned int i = 0;
	for (i = 0; i < array_num(old->as_regions); i++) {
		struct region* reg = array_get(old->as_regions, i);
		struct region* newReg = (struct region*) kmalloc(sizeof(struct region));
		newReg->executable = reg->executable;
		newReg->readable = reg->readable;
		newReg->rg_size = reg->rg_size;
		newReg->rg_vaddr = reg->rg_vaddr;
		newReg->writeable = reg->writeable;
		unsigned int idx;
		array_add(newas->as_regions, newReg, &idx);
	}
	for (i = 0; i < array_num(old->as_pagetable); i++) {
		struct page* pg = array_get(old->as_pagetable, i);
		struct page* newPg = page_create(newas, pg->pt_virtbase * PAGE_SIZE);
		memmove(PADDR_TO_KVADDR((void*) (newPg->pt_pagebase * PAGE_SIZE)),
				PADDR_TO_KVADDR((void*) (pg->pt_pagebase * PAGE_SIZE)),
				PAGE_SIZE);
	}
	*ret = newas;
	return 0;
}

void as_destroy(struct addrspace *as) {
	/*
	 * Clean up as needed.
	 */
	int regionCount = array_num(as->as_regions);
	int i;
	for (i = 0; i < regionCount; i++) {
		struct region* reg = array_get(as->as_regions, 0);
		kfree(reg);
		array_remove(as->as_regions, 0);
	}
	array_destroy(as->as_regions);

	int pageCount = array_num(as->as_pagetable);
	for (i = 0; i < pageCount; i++) {
		struct page* pg = array_get(as->as_pagetable, 0);
		coremap_freeuserpages(pg->pt_pagebase * PAGE_SIZE);
		kfree(pg);
		array_remove(as->as_pagetable, 0);
	}
	array_destroy(as->as_pagetable);

	kfree(as);
}

void as_activate(void) {
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	/*
	 * Write this.
	 */
	int spl = splhigh();
	int i;
	for (i = 0; i < NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	splx(spl);
}

void as_deactivate(void) {
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		int readable, int writeable, int executable) {
	/*
	 * Write this.
	 */
	struct region* newregion = (struct region*) kmalloc(sizeof(struct region));

	newregion->executable = executable;
	newregion->readable = readable;
	newregion->writeable = writeable;

	newregion->rg_size = memsize;
	newregion->rg_vaddr = vaddr;
	unsigned int index;
	array_add(as->as_regions, newregion, &index);
	// adjust this to be the next closest multiple of 4096
	as->as_addrPtr = vaddr + memsize;
	if (as->as_addrPtr % PAGE_SIZE != 0) {
		as->as_addrPtr = ((as->as_addrPtr / PAGE_SIZE) + 1) * PAGE_SIZE;
		// an ugly way round it to a multiple of 4096
	}
	/*kprintf("New region created : as id : %d start = %x, end = %x, addrPtr = %x\n",
			as->as_id, newregion->rg_vaddr, newregion->rg_vaddr + newregion->rg_size,
			as->as_addrPtr);*/
	return 0;
}

int as_prepare_load(struct addrspace *as) {
	/*
	 * Write this.
	 */

	(void) as;
	return 0;
}

int as_complete_load(struct addrspace *as) {
	/*
	 * Write this.
	 */

	(void) as;
	return 0;
}

int as_define_stack(struct addrspace *as, vaddr_t *stackptr) {

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;
	as->as_stackPageCount = 0;
	return 0;
}

struct page* page_create(struct addrspace* as, vaddr_t faultaddress) {
	struct page* newpage = (struct page*) kmalloc(sizeof(struct page));
	newpage->pt_virtbase = faultaddress / PAGE_SIZE;
	newpage->pt_pagebase = coremap_allocuserpages(1, as) / PAGE_SIZE;
	if(newpage->pt_pagebase == 0) {
		kfree(newpage);
		return NULL;
	}
	unsigned int idx;
	array_add(as->as_pagetable, newpage, &idx);
	return newpage;
}

