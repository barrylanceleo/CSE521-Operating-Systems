/**
 * proc_syscalls.c
 *
 * Implementation for process related system calls
 *
 */

#include <types.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <kern/errno.h>
#include <processtable.h>
#include <synch.h>
#include <cpu.h>
#include <syscall.h>
#include <copyinout.h>
#include <limits.h>
#include <mips/trapframe.h>
#include <addrspace.h>
#include <kern/wait.h>
#include "../include/types.h"

int sys_getpid(pid_t* retval) {
	*retval = curproc->p_pid;
	return 0;
}

int sys_fork(struct trapframe* tf, pid_t* retval) {
	int result;
	struct addrspace *ad;
	struct proc *child = proc_createchild(curproc, &ad);
	if (child == NULL) {
		*retval = -1;
		return ENOMEM;
	}
	//kprintf("TEMPPPP:In fork Child PID IS %d\n", child->p_pid);

	result = as_copy(curproc->p_addrspace, &(child->p_addrspace));
	if (result) {
		proc_destroy(child);
		*retval = -1;
		return ENOMEM;
	}

	struct trapframe* child_tf = (struct trapframe*) kmalloc(
			sizeof(struct trapframe));
	if (child_tf == NULL) {
		proc_destroy(child);
		kfree(child_tf);
		*retval = -1;
		return ENOMEM;
	}
	*child_tf = *tf;

	lock_acquire(child->p_opslock);
	result = thread_fork("Child proc", child, enter_forked_process,
			(struct trapframe *) child_tf,
			(unsigned long) (child->p_addrspace));

	if (result) {
		kfree(child_tf);
		*retval = -1;
		lock_release(child->p_opslock);
		proc_destroy(child);
		return ENOMEM;
	}

	*retval = child->p_pid;
	lock_release(child->p_opslock);
	return 0;
	/*	int ret = 0;
	 struct proc* parent = curproc;
	 kprintf("TEMPPPP:HERE FORK\n");
	 struct addrspace *as;
	 struct proc* child = proc_createchild(parent, &as);
	 if (child == NULL) {
	 return ENOMEM;
	 }
	 struct trapframe* copytf =  kmalloc(sizeof(struct trapframe));
	 *copytf = *tf;
	 kprintf("TEMPPPP:after create child\n");
	 struct thread* currentthread = curthread;
	 ret = thread_fork(currentthread->t_name, child, enter_forked_process,
	 (void*) copytf, (unsigned long) as);

	 if (ret) {
	 proc_destroy(child);
	 return ENOMEM;
	 }
	 kprintf("TEMPPPP:after thread fork\n");
	 *pid = child->p_pid;
	 return ret;*/
}

char USER_PC_ARG[ARG_MAX];

static int copyargstokernel(userptr_t uargs, char** argv, unsigned long* argc) {
	*argv = USER_PC_ARG;

	userptr_t uarg_iter = uargs;
	userptr_t uaddress;
	*argc = 0;
	char* buf = *argv;
	int readlen = 0;
	while (uarg_iter != NULL) {

		int v = copyin(uarg_iter, &uaddress, sizeof(char*));
		if (v && uarg_iter == uargs) {
			return EFAULT;
		}
		size_t len = 0;
		if ((unsigned int) uaddress == 0x40000000
				|| (unsigned int) uaddress == 0x80000000) {
			return EFAULT;
		}
		int w = copyinstr(uaddress, buf, ARG_MAX - readlen, &len);
		//kprintf("TEMPPPP: '%s' @%p, '%s' @%p = buf!!!\n", *argv , *argv, buf, buf);

		(void) w;

		if (len == 0) {
			//kprintf("TEMPPPP: %lu = argc!!!\n", *argc);
			return 0;
		}

		readlen += len;
		buf += len;
		/*kargv[*argc] = (char*) kmalloc(sizeof(char) * len);
		 strcpy(kargv[*argc], buf);*/
		(*argc)++;
		/*if(*argc == 399) {
		 char** temp = (char**)kmalloc(ARG_MAX*sizeof(char*));
		 *argv = temp;
		 memmove(argv, kargv, sizeof(char*)*(*argc));
		 kargv = *argv;
		 }*/
		uarg_iter += sizeof(userptr_t);

	}
	return 0;
}

int sys_execv(userptr_t program, userptr_t args, int32_t* retval) {
	int result;
	*retval = -1;

	if (program == NULL || args == NULL) {
		kprintf("EXECV : PROGRAM OR ARGS NULL\n");
		return EFAULT;
	}

	char k_progname[FILE_NAME_MAXLEN];
	size_t size;
	if ((result = copyinstr(program, k_progname, FILE_NAME_MAXLEN, &size))
			!= 0) {
		kprintf("EXECV : COPYINSTR NON ZERO\n");
		return EFAULT;
	}

	if (size <= 1) { // apparently an empty string has one character, '\0'...
		kprintf("EXECV : SIZE IS 1\n");
		return EINVAL;
	}

	char* progname = k_progname;
	unsigned long argc;
	char* argv;

	result = copyargstokernel(args, &argv, &argc);
	if (result != 0) {
		kprintf("EXECV : result of copyargs was not zero %d\n", result);
		return result;
	}

	if (argc == 0) {
		return runprogram2(progname, NULL, 0);
	} else {
		return runprogram2(progname, &argv, argc);
	}
}

static int isParent(pid_t k_pid) {
	struct proc * p;
	lookup_processtable(k_pid, &p);
	if (p == NULL) {
		return 2;
	}
	if (p->p_ppid != curproc->p_pid) {
		return 1;
	}
	return 0;
}

int k_waitpid(pid_t k_pid, int* status, pid_t* retval) {
	int result = 0;
	struct proc* targetprocess;
	switch (isParent(k_pid)) {
	case 1:
		*retval = -1;
		return ECHILD;
	case 2:
		*retval = -1;
		return ESRCH;
	}
	lookup_processtable(k_pid, &targetprocess);
	lock_acquire(targetprocess->p_waitcvlock);
	while (true) {
		if (targetprocess->p_state == PS_COMPLETED) {
			*status = targetprocess->p_returnvalue;
			lock_release(targetprocess->p_waitcvlock);
			proc_destroy(targetprocess);
			*retval = k_pid;
			return result;
		}
		cv_wait(targetprocess->p_waitcv, targetprocess->p_waitcvlock);
	}
	lock_release(targetprocess->p_waitcvlock);
	return -1; // this code must be unreachable
}

int sys_waitpid(userptr_t userpid, userptr_t status, userptr_t options,
		pid_t* retval) {

	int result = 0;
	int k_status = 0;

	// check for null pointer reference
	if (status == NULL) {
		return result;
	}

	int k_options = (int) options;
	if (k_options != 0) {
		*retval = -1;
		return EINVAL;
	}

	pid_t k_pid = (pid_t) userpid;

	if (k_pid < PID_MIN) {
		return ESRCH;
	}

	if (k_pid > PID_MAX) {
		return ESRCH;
	}

	if (k_pid == curproc->p_pid) {
		return ECHILD;
	}

	if (k_pid == curproc->p_ppid) {
		return ECHILD;
	}

	//kprintf("TEMPPPP Starting wait_pid for pid: %d", k_pid);

	result = k_waitpid(k_pid, &k_status, retval);

	// check for bad pointer reference

	int err_code = 0;
	err_code = copyout(&k_status, status, sizeof(int));
	if (err_code) {
		*retval = -1;
		return err_code;
	}
	return result;

}

int k_exit(int exitcode) {
	int result = 0;
	struct proc* curprocess = curproc;

	lock_acquire(curprocess->p_waitcvlock);
	curprocess->p_returnvalue = exitcode;
	curprocess->p_state = PS_COMPLETED;
	//kprintf("TEMPPPP: PS Set to completed %d in pid: %d\n", exitcode, curprocess->p_pid);
	as_destroy(curprocess->p_addrspace);
	curprocess->p_addrspace = NULL;
	cv_broadcast(curprocess->p_waitcv, curprocess->p_waitcvlock);
	lock_release(curprocess->p_waitcvlock);

	thread_exit(); // stop execution of current thread

	return result; // Will not be executed
}

int sys__exit(int exitcode) {
	return k_exit(_MKWAIT_EXIT(exitcode));
}

