/*
 * Copyright (c) 2013
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

#ifndef _PROC_H_
#define _PROC_H_

/*
 * Definition of a process.
 *
 * Note: curproc is defined by <current.h>.
 */

#include <spinlock.h>
#include <array.h>
#include <synch.h>
#include <kern/types.h>
struct addrspace;
struct thread;
struct vnode;

enum process_state {
	PS_RUNNING,
	PS_COMPLETED
};

/*
 * Process structure.
 *
 * Note that we only count the number of threads in each process.
 * (And, unless you implement multithreaded user processes, this
 * number will not exceed 1 except in kproc.) If you want to know
 * exactly which threads are in the process, e.g. for debugging, add
 * an array and a sleeplock to protect it. (You can't use a spinlock
 * to protect an array because arrays need to be able to call
 * kmalloc.)
 *
 * You will most likely be adding stuff to this structure, so you may
 * find you need a sleeplock in here for other reasons as well.
 * However, note that p_addrspace must be protected by a spinlock:
 * thread_switch needs to be able to fetch the current address space
 * without sleeping.
 */
struct proc {
	char *p_name;			/* Name of this process */
	struct spinlock p_lock;		/* Lock for this structure */
	unsigned p_numthreads;		/* Number of threads in this process */

	/* VM */
	struct addrspace *p_addrspace;	/* virtual address space */

	/* VFS */
	struct vnode *p_cwd;		/* current working directory */

	/* add more material here as needed */

	struct array *p_filetable;

	int p_fdcounter;

	/* Process operations support */

	int p_pid; // process id
	int p_ppid; // parent process id

	struct cv* p_waitcv;  // to handle wait pid
	struct lock* p_waitcvlock;

	enum process_state p_state;
	int p_returnvalue; // if process completed this variable has its return value
};

struct file_handle
{
	int fh_offset;
	int fh_permission;
	struct vnode* fh_vnode;
};

struct filetable_entry
{
	int ft_fd;
	struct file_handle* ft_handle;
};


/* This is the process structure for the kernel and for kernel-only threads. */
extern struct proc *kproc;

/* Call once during system startup to allocate data structures. */
void proc_bootstrap(void);

/* Create a fresh process for use by runprogram(). */
struct proc *proc_create_runprogram(const char *name);

/* Create a child process for the passed process. */
struct proc *proc_createchild(struct proc* process);

/* Destroy a process. */
void proc_destroy(struct proc *proc);

/* Attach a thread to a process. Must not already have a process. */
int proc_addthread(struct proc *proc, struct thread *t);

/* Detach a thread from its process. */
void proc_remthread(struct thread *t);

/* Fetch the address space of the current process. */
struct addrspace *proc_getas(void);

/* Change the address space of the current process, and return the old one. */
struct addrspace *proc_setas(struct addrspace *);

/* Reset the fd count of process to zero and empty filetable*/
void proc_resetfdcount(struct proc* process);

/* Generate a new file descriptor for the current process */
int proc_generatefd(struct proc* process);

/* Open standard fd's of stdin stdout and stderr*/
int proc_openstandardfds(struct proc* process);

///* Get the index of the fd from the filetable*/
//unsigned int getftarrayindex(struct array* ft, int fd);

/* Empty the contents of the file table */
void filetable_empty(struct array* ft);

/* Add a new entry to the file table, return the inserted fd */
int filetable_addentry (struct proc* process, char* filename, int flags, int permission);

/* Lookup an fd in the filetable*/
struct filetable_entry *filetable_lookup(struct array* ft, int fd);

/* remove an entry from the file table filetable, destroys filehandle and free memory*/
int filetable_remove(struct array* ft, int fd);

/* close the vnode and free memory*/
void filehandle_destroy (struct file_handle* handle);

/* run a program*/
int runprogram2(char *progname, char** argv, unsigned long argc);

#endif /* _PROC_H_ */
