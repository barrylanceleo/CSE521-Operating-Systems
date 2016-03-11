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

/*
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */

#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <vfs.h>
#include <processtable.h>
#include <kern/fcntl.h>
#include <kern/errno.h>
/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

/*
 * Create a proc structure.
 */
static struct proc *
proc_create(const char *name) {
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

	proc->p_numthreads = 0;
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

	/** file table */
	proc->p_filetable = array_create();
	if (proc->p_filetable == NULL) {
		kfree(proc->p_name);
		kfree(proc);

			return NULL;
	}
	proc->p_waitcvlock = lock_create(name);
	if (proc->p_waitcvlock == NULL) {
				kfree(proc->p_name);
				array_destroy(proc->p_filetable);
				kfree(proc);
				return NULL;
	}

	proc->p_waitcv = cv_create(name);
	if (proc->p_waitcv == NULL) {
			kfree(proc);
			return NULL;
	}

	proc->p_opslock = lock_create(name);
	if (proc->p_opslock == NULL) {
			kfree(proc);
			return NULL;
	}
	proc->p_state = PS_RUNNING;


	proc->p_returnvalue = -1;

	proc->p_fdcounter = 0;

	// add the process to the process table

	return proc;
}

/*
 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 */
void proc_destroy(struct proc *proc) {
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		} else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}

	//TODO destroy everything created in create

	KASSERT(proc->p_numthreads == 0);
	spinlock_cleanup(&proc->p_lock);

	cv_destroy(proc->p_waitcv);
	lock_destroy(proc->p_waitcvlock);

	lock_destroy(proc->p_opslock);

	as_deactivate();

	// TODO destroy the AS

	kfree(proc->p_name);
	kfree(proc);
}

/*
 * Create the process structure for the kernel.
 */
void proc_bootstrap(void) {
	kproc = proc_create("[kernel]");
	if (kproc == NULL) {
		panic("proc_create for kproc failed\n");
	}
	init_processtable();
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */
struct proc *
proc_create_runprogram(const char *name) {
	struct proc *newproc;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	addTo_processtable(newproc);

	/* VM fields */

	newproc->p_addrspace = NULL;

	if(newproc->p_fdcounter == 0) {
	/* Create the standard fds */
	proc_openstandardfds(newproc);
	} else {
		newproc->p_ppid = curproc->p_pid;
	}

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	return newproc;
}

struct proc* proc_createchild(struct proc* parent, struct addrspace** as) {
	struct proc* child = proc_create(parent->p_name);
	if (child == NULL) {
		return NULL;
	}

	unsigned int i;
	// TODO Move this to a separate method
	for (i = 0; i < array_num(parent->p_filetable); i++) {
		int s = array_add(child->p_filetable, array_get(parent->p_filetable, i), NULL);
		if(s == ENOMEM) {
			proc_destroy(child);
			return NULL;
		}
	}
	child->p_fdcounter = parent->p_fdcounter;

	/** process table */
	if(addTo_processtable(child) != 0)
	{
		return NULL;
	}
	child->p_ppid = parent->p_pid;

	/** cwd */
	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&(parent->p_lock));
	if (parent->p_cwd != NULL) {
		VOP_INCREF(parent->p_cwd);
		child->p_cwd = parent->p_cwd;
	}
	spinlock_release(&(parent->p_lock));

	/** address space */
	//as_copy(parent->p_addrspace, as);
	(void)as;
	return child;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int proc_addthread(struct proc *proc, struct thread *t) {
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	proc->p_numthreads++;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = proc;
	splx(spl);

	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void proc_remthread(struct thread *t) {
	struct proc *proc;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	KASSERT(proc->p_numthreads > 0);
	proc->p_numthreads--;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = NULL;
	splx(spl);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void) {
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas) {
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}

void proc_resetfdcount(struct proc* process) {
	process->p_fdcounter = 0;
	filetable_empty(process->p_filetable);
}

int proc_generatefd(struct proc* process) {
	return process->p_fdcounter++;
}

static int filetable_addfd(struct proc* process, struct filetable_entry* entry) {
	unsigned int index;
	int result = array_add(process->p_filetable, entry, &index);
	return result;
}

static int filetable_addentryforvnode(struct proc* process, int permission,
		struct vnode* vn) {

	struct file_handle* handle = (struct file_handle*) kmalloc(
			sizeof(struct file_handle));
	handle->fh_offset = 0;
	handle->fh_permission = permission;
	handle->fh_vnode = vn;

	struct filetable_entry* entry = (struct filetable_entry*) kmalloc(
			sizeof(struct filetable_entry));
	entry->ft_fd = proc_generatefd(process);
	entry->ft_handle = handle;

	filetable_addfd(process, entry);
	return entry->ft_fd;
}

static struct vnode* console_vnode = NULL;

int proc_openstandardfds(struct proc* process) {
	if (process->p_fdcounter != 0) {
		panic(
				"opening standard fds while counter is not 0! (it is actually %d)",
				process->p_fdcounter);
	}
	if (console_vnode == NULL) {
//		kprintf("TEMPPPP: CREATING STANDARD FDS\n");
		if (vfs_open((char*) "con:", 0, O_RDWR, &console_vnode)) {
			kprintf("stdin open fail\n");
			return -1;
		}
	}
	//kprintf("TEMPPPP: INSIDE OPEN STANDARD FDs\n");
	filetable_addentryforvnode(process, O_RDONLY, console_vnode);
	filetable_addentryforvnode(process, O_WRONLY, console_vnode);
	filetable_addentryforvnode(process, O_WRONLY, console_vnode);
	//kprintf("TEMPPPP: END OF OPEN STANDARD FDs, file table size is %d\n",
	//		array_num(process->p_filetable));
	return 0;
}

int filetable_addentry(struct proc* process, char* filename, int flags,
		int mode) {
	struct vnode* vn;
	int result = 0;
	mode = 0;
	if ((result = vfs_open(filename, flags, mode, &vn)) != 0) {
		return result;
	}

	return filetable_addentryforvnode(process, flags, vn);

}

static int getftarrayindex(struct array* ft, int fd) {
	unsigned int i;
	unsigned int ft_len = array_num(ft);
	for (i = 0; i < ft_len; i++) {
		if (((struct filetable_entry*) array_get(ft, i))->ft_fd == fd) {
			return i;
		}
	}
	return -1;
}

struct filetable_entry *filetable_lookup(struct array* table, int fd) {

	if (table == 0) {
		return NULL;
	}

	int index = getftarrayindex(table, fd);
	if (index < 0) {
		return NULL;
	}

	struct filetable_entry* entry = (struct filetable_entry*) array_get(table,
			index);
	return entry;

}

int filetable_remove(struct array* table, int fd) {

	if (table == 0) {
		return -1;
	}


	int index = getftarrayindex(table, fd);
	if (index < 0) {
		return -1;
	}

	struct filetable_entry* entry = (struct filetable_entry*) array_get(table,
			index);

	filehandle_destroy(entry->ft_handle);

	// free memory allocated for the entry
	kfree(entry);

	array_remove(table, index);

	return 0;

}

void filetable_empty(struct array* ft) {
	unsigned int i;
	for (i = 0; i < array_num(ft); i++) {
		struct filetable_entry* entry = (struct filetable_entry*) array_get(ft,
				i);

		filehandle_destroy(entry->ft_handle);

		// free memory allocated for the entry
		kfree(entry);

		// remove the entry from the filetable
		array_remove(ft, i);

	}

}

void filehandle_destroy(struct file_handle* handle) {

	// close the vnode based on the refcount
	vfs_close(handle->fh_vnode);
	kfree(handle);
}

