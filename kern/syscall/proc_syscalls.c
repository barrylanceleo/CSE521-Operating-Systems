/**
 * proc_syscalls.c
 *
 * Implementation for process related system calls
 *
 */

#include <current.h>
#include <process.h>

int sys_getpid(pid_t* retval) {
	*retval = curproc->p_pid;
	return 0;
}
