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

/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than runprogram() does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <copyinout.h>

static void copyoutargv(userptr_t uargv, char** argv, int argc,
		vaddr_t* stackptr) {
	int i = 0;
	int offset = (argc + 1)* 4;
	for (i = 0; i < argc; i++) {
		int len = (strlen(argv[i]) + 1) / 4 + 1;
		offset += len * 4;
	}
	*stackptr = (vaddr_t)(uargv - offset); // Dont touch stackptr from here
	kprintf("TEMPPPP: stackptr = uargv - offset [%p=%p-%d]\n", (void*)*stackptr, uargv,
			offset);

	userptr_t address_ptr = (userptr_t)*stackptr;
	userptr_t buf_ptr = uargv;

	for (i = 0; i < argc; i++) {
		int strsize = strlen(argv[i]);
		buf_ptr = buf_ptr - (((strsize / 4) + 1) * 4);
		userptr_t bufstr = buf_ptr;
		kprintf("TEMPPPP: ptr = addr [%p=>%p] %d\n", (void*)buf_ptr, (void*)address_ptr, i);
		copyout(&buf_ptr, address_ptr, 4);
		address_ptr += 4;

		char buf[5];
		buf[4] = '\0';
		int j = 0;
		while (strsize > 0) {
			for (int k = 0; k < 4; k++) {
				buf[k] = strsize > 0 ? argv[i][j++] : '\0';
				strsize--;
			}
			kprintf("TEMPPPP: buf = addr [%s=>%p]\n", buf, (void*)bufstr);
			copyout(buf, bufstr, 4);
			bufstr += 4;
			if (strsize == 0) {
				strsize++;
			}
		}
	}
}

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int runprogram2(char *progname, char** argv, unsigned long argc) {
	(void) argv;
	(void) argc;
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
//		kprintf("TEMPPPP:runprogram.c vfs open failed!!\n");
		return result;
	}
//	kprintf("TEMPPPP:runprogram.c after vfs open!!\n");

	/* We should be a new process. */
	//KASSERT(proc_getas() == NULL);
	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	struct addrspace *oldas = proc_setas(as);
	if (oldas) {
		as_destroy(oldas);
	}

	as_activate();

//	kprintf("TEMPPPP:runprogram.c after address space activation!!\n");

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}
	userptr_t uargv = NULL;
	if (argc > 0) {
		uargv = (userptr_t) stackptr;
		copyoutargv(uargv, argv, argc, &stackptr);
		uargv = (userptr_t) stackptr;
	}

//	kprintf("TEMPPPP:runprogram.c Entering new process!!\n");

	/* Warp to user mode. */
	enter_new_process(argc > 0? argc  : 0 /*argc*/, uargv /*userspace addr of argv*/,
	NULL /*userspace addr of environment*/, stackptr, entrypoint);

	/* enter_new_process does not return. */
	return EINVAL;
}

int runprogram(char *progname) {
	return runprogram2(progname, NULL, 0);
}
