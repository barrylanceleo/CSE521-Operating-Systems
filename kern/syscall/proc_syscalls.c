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
#include <syscall.h>
#include <copyinout.h>
#include <limits.h>
#include <mips/trapframe.h>
#include <addrspace.h>

int sys_getpid(pid_t* retval) {
	*retval = curproc->p_pid;
	return 0;
}

int sys_fork(struct trapframe* tf, pid_t* retval) {
	int error;
	struct proc *child_proc = proc_create_runprogram("child process");
	struct trapframe* child_trapframe = NULL;

	error = as_copy(curproc->p_addrspace, &(child_proc->p_addrspace));
	if(error){
		*retval = -1;
		return error;
	}
	child_trapframe = (struct trapframe*)kmalloc(sizeof(struct trapframe));
	if(child_trapframe == NULL){
		*retval = -1;
		return ENOMEM;
	}
	*child_trapframe = *tf;
	kprintf("TEMPPPP:PPID IS %d\n", curproc->p_pid);

	error = thread_fork("Child proc", child_proc, enter_forked_process,
		(struct trapframe *)child_trapframe,(unsigned long)child_proc->p_addrspace);

	if(error){
		kfree(child_trapframe);
		as_destroy(child_proc->p_addrspace);
		*retval = -1;
		return error;
	}

	*retval = child_proc->p_pid;
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

static void copyargstokernel(userptr_t uargs, char** kargv, unsigned long* argc) {
	userptr_t uarg_iter = uargs;
	userptr_t uaddress;
	int count = 0;
	char buf[ARG_MAX];
	while (uarg_iter != NULL) {

		copyin(uarg_iter, &uaddress, sizeof(char*));
		size_t len = 0;
		copyinstr(uaddress, buf, ARG_MAX, &len);
		kargv[count] = (char*) kmalloc(sizeof(char) * len);
		strcpy(kargv[count], buf);
		count++;
		uarg_iter++;
	}
	*argc = count;
}

int sys_execv(userptr_t program, userptr_t args) {
	int result;

	kprintf("TEMPPPP:EXECV CALLED MAN!!!\n");
	char k_progname[FILE_NAME_MAXLEN];
	if ((result = copyinstr(program, k_progname, FILE_NAME_MAXLEN, 0)) != 0) {
		return result;
	}

	char* progname = k_progname;
	char* argv[ARG_MAX];
	unsigned long argc;

	copyargstokernel(args, argv, &argc);

	runprogram2(progname, argv, argc);
	return result;
}

int k_waitpid(pid_t k_pid, int* status, pid_t* retval) {
	int result = 0;
	struct proc* targetprocess;
	lookup_processtable(k_pid, &targetprocess);
	lock_acquire(targetprocess->p_waitcvlock);
	while (true) {
		if (targetprocess->p_state == PS_COMPLETED) {
			kprintf("TEMPPPP PS Completed:\n");
			*status = targetprocess->p_returnvalue;
			lock_release(targetprocess->p_waitcvlock);
			proc_destroy(targetprocess);
			*retval = k_pid;
			return result;
		}
		cv_wait(targetprocess->p_waitcv, targetprocess->p_waitcvlock);
	}
	return -1; // this code must be unreachable
}

int sys_waitpid(userptr_t userpid, userptr_t status, userptr_t options,
		pid_t* retval) {

	int k_options;

	copyin(options, &k_options, sizeof(int));

	if (k_options != 0) {
		*retval = -1;
		return -1;
	}

	pid_t k_pid;
	int result = 0;
	result = copyin(userpid, &k_pid, sizeof(pid_t));
	*retval = k_pid;
	int k_status;
	result = k_waitpid(k_pid, &k_status, retval);
	result = copyout(&k_status, status, sizeof(int));
	return result;

}

int sys__exit(int exitcode) {
	int result = 0;
	struct proc* curprocess = curproc;

	lock_acquire(curprocess->p_waitcvlock);
	curprocess->p_returnvalue = exitcode;
	curprocess->p_state = PS_COMPLETED;
	kprintf("TEMPPPP: PS Set to completed %d\n", exitcode);
	cv_broadcast(curprocess->p_waitcv, curprocess->p_waitcvlock);
	lock_release(curprocess->p_waitcvlock);

	thread_exit(); // stop execution of current thread

	return result; // Will not be executed
}

