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


int sys_getpid(pid_t* retval) {
	*retval = curproc->p_pid;
	return 0;
}

int sys_fork(struct trapframe* tf, pid_t* pid) {
	int ret = 0;
	struct proc* parent = curproc;
	struct proc* child = proc_createchild(parent);
	if (child == NULL) {
		return ENOMEM;
	}
	struct thread* currentthread = curthread;
	ret = thread_fork(currentthread->t_name, child, enter_forked_process, (void*)&tf,
			(unsigned long)1);
	if (ret) {
		proc_destroy(child);
		return ENOMEM;
	}
	*pid = child->p_pid;
	return ret;
}

static void copyargstokernel(userptr_t uargs, char** kargv, unsigned long* argc) {
	userptr_t uarg_iter = uargs;
	userptr_t uaddress;
	int count = 0;
	char buf [ARG_MAX];
	while (uarg_iter != NULL) {

		copyin(uarg_iter, &uaddress, sizeof(char*));
		size_t len = 0;
		copyinstr(uaddress, buf, ARG_MAX, &len);
		kargv[count] = (char*)kmalloc(sizeof(char)*len);
		strcpy(kargv[count], buf);
		count++;
		uarg_iter++;
	}
	*argc = count;
}

int sys_execv(userptr_t program, userptr_t args) {
	int result;

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

int sys_waitpid(userptr_t userpid, userptr_t status, userptr_t options, pid_t* retval) {

	if(options != 0){
		*retval = -1;
		return -1;
	}

	pid_t k_pid;
	int result = 0;
	result = copyin(userpid, &k_pid, sizeof(pid_t));
	*retval = k_pid;
	struct proc* targetprocess;
	lookup_processtable(k_pid, &targetprocess);
	lock_acquire(targetprocess->p_waitcvlock);
	while (true) {
		if (targetprocess->p_state == PS_COMPLETED) {
			result = copyout(&targetprocess->p_returnvalue, status, sizeof(int));
			lock_release(targetprocess->p_waitcvlock);
			proc_destroy(targetprocess);
			return result;
		}
		cv_wait(targetprocess->p_waitcv, targetprocess->p_waitcvlock);
	}
	return -1;  // this code must be unreachable
}

int sys__exit(userptr_t exitcode)
{
	int result = 0;
	int k_exitcode = copyin(exitcode, &k_exitcode, sizeof(int));
	struct proc* curprocess = curproc;

	lock_acquire(curprocess->p_waitcvlock);
	curprocess->p_returnvalue = k_exitcode;
	curprocess->p_state = PS_COMPLETED;
	cv_broadcast(curprocess->p_waitcv, curprocess->p_waitcvlock);
	lock_release(curprocess->p_waitcvlock);

	thread_exit(); // stop execution of current thread

	return result; // Will not be executed
}

