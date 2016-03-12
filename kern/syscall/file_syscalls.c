//
// Created by barry on 3/2/16.
//
#include <types.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <proc.h>
#include <vfs.h>
#include <vnode.h>
#include <stat.h>
#include <uio.h>
#include <current.h>
#include <array.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/seek.h>

int sys_open(userptr_t file_name, int arguments, int mode, int32_t* retval) {

	int result = 0;
	*retval = -1;


	//kprintf("TEMPPPP: CWDDDDDDDD %p\n",curproc->p_cwd);

	// verify if the flag is valid

	if(arguments > 128)  // highest flag is 64
	{
		*retval = EINVAL;
		return EINVAL;
	}

	if((arguments | O_EXCL) && !(arguments | O_CREAT))
	{
		*retval = EINVAL;
		return EINVAL;
	}

	struct proc* curprocess = curproc;
	// copy the file_name to the kernel space
	char k_filename[FILE_NAME_MAXLEN + 1];
	if ((result = copyinstr(file_name, k_filename, FILE_NAME_MAXLEN + 1, 0))
			!= 0) {
		*retval = result;
		return result;
	}

	// insert file handle to the filetable and get the fd
	int new_fd;
	result = filetable_addentry(curprocess, k_filename, arguments, mode, &new_fd);
	if(result < 0)
	{
		*retval = result;
		return result;

	}

	//kprintf("TEMPPPP: %d: open fd %d , %p\n",curproc->p_pid, new_fd, curprocess->p_filetable);
	*retval = new_fd;
	return result;
}

int sys_read(int read_fd, userptr_t user_buf_ptr, int buflen, int32_t* retval) {
	int result = 0;
	*retval = -1;
	struct proc* curprocess = curproc;
	size_t read_buflen = buflen;

	// get the file table entry for the fd
	struct filetable_entry* read_ft_entry = filetable_lookup(
			curprocess->p_filetable, read_fd);

	if (read_ft_entry == NULL) {
		kprintf("TEMPPPP:READ 1\n");
		return EBADF;
	}

	struct file_handle* handle = read_ft_entry->ft_handle;
	struct vnode* file_vnode = handle->fh_vnode;

	//check if the file handle has read permission
	if (!((handle->fh_permission & 3) == O_RDONLY || (handle->fh_permission & 3) == O_RDWR))
	{

		kprintf("TEMPPPP:No Read permission in sys read %d\n",
				handle->fh_permission);
		return EBADF;
	}


	if(read_ft_entry->ft_fd  < 0 || read_ft_entry->ft_fd > 2){
			//kprintf("TEMPPPP:Read from fd: %d at offset: %llu\n", read_ft_entry->ft_fd, handle->fh_offset);
	}


	// lock the operation
	lock_acquire(file_vnode->vn_opslock);

	//read the data to the uio

	// write the data from the uio to the file
	struct iovec iov;
	struct uio kuio;
	iov.iov_ubase = user_buf_ptr;
	iov.iov_len = read_buflen; // length of the memory space
	kuio.uio_iov = &iov;
	kuio.uio_iovcnt = 1;
	kuio.uio_resid = read_buflen; // amount to read from the file
	kuio.uio_offset = read_ft_entry->ft_handle->fh_offset;
	kuio.uio_segflg = UIO_USERSPACE;
	kuio.uio_rw = UIO_READ;
	kuio.uio_space = curproc->p_addrspace;
	result = VOP_READ(file_vnode, &kuio);
	if (result) {
		//kprintf("TEMPPPP: %d : INSIDE read(invalid fd)  %d\n",curproc->p_pid, (int)read_fd);
		// release lock on the vnode
		lock_release(file_vnode->vn_opslock);
		return result;
	}

	// update the offset in the file table
	handle->fh_offset += read_buflen - kuio.uio_resid;

	// update the retval to indicate the number of bytes read
	*retval = read_buflen - kuio.uio_resid;

	// release lock on the vnode
	lock_release(file_vnode->vn_opslock);

	return result;

}

int sys_write(int write_fd, userptr_t user_buf_ptr, int nbytes, int32_t* retval) {

	int result = 0;
	*retval = -1;
	struct proc* curprocess = curproc;
	ssize_t write_nbytes = nbytes;
	//kprintf("TEMPPPP: %d : INSIDE write  %d   %p\n",curproc->p_pid, (int)write_fd, curprocess->p_filetable);
	// get the file table entry for the fd
	struct filetable_entry* entry = filetable_lookup(curprocess->p_filetable,
			write_fd);
	if (entry == NULL) {
		//kprintf("TEMPPPP: %d : INSIDE write(invalid fd)  %d\n",curproc->p_pid, (int)write_fd);
		return EBADF;
	}

	struct file_handle* handle = entry->ft_handle;
	struct vnode* file_vnode = handle->fh_vnode;

	//check if the file handle has write has permissions
	if (!(handle->fh_permission & 3)) {
		kprintf("Invalid write permission on file for write\n");
		return EBADF;
	}

/*	if(entry->ft_fd > 2){
		kprintf("TEMPPPP:Write to fd: %d at offset: %llu\n", entry->ft_fd, handle->fh_offset);
	}*/
//
//	if(write_nbytes > 1)
//	{
//		kprintf("TEMPPPP: CWDDDDDDDD %p\n",curproc->p_cwd);
//		kprintf("TEMPPPP: Trying to write %d bytes to fd %d\n",write_nbytes, write_fd);
//	}




	// lock the operation
	lock_acquire(file_vnode->vn_opslock);

	// write the data from the uio to the file
	struct iovec iov;
	struct uio kuio;
	iov.iov_ubase = user_buf_ptr;
	iov.iov_len = write_nbytes; // length of the memory space
	kuio.uio_iov = &iov;
	kuio.uio_iovcnt = 1;
	kuio.uio_resid = write_nbytes; // amount to read from the file
	kuio.uio_offset = entry->ft_handle->fh_offset;
	kuio.uio_segflg = UIO_USERSPACE;
	kuio.uio_rw = UIO_WRITE;
	kuio.uio_space = curprocess->p_addrspace;
	result = VOP_WRITE(file_vnode, &kuio);
	if (result) {
		// release lock on the vnode
		lock_release(file_vnode->vn_opslock);
		return result;
	}

	// update the offset in the file table
	*retval = write_nbytes - kuio.uio_resid;
	handle->fh_offset += *retval;

	// update the retval to indicate the number of bytes read
	// release lock on the vnode
	lock_release(file_vnode->vn_opslock);

//	if(write_nbytes > 1)
//	{
//	kprintf("TEMPPPP: Wrote %d bytes to fd %d\n",*retval, write_fd);
//
//	}

	return result;
}

int sys_close(userptr_t fd, int32_t* retval) {
	//kprintf("TEMPPPP: %d : INSIDE CLOSE   %d   %p\n",curproc->p_pid, (int)fd, curproc->p_filetable);
	*retval = -1;
	struct proc* curprocess = curproc;

	if (filetable_remove(curprocess->p_filetable, (int)fd) == -1) {
		return EBADF;
	}

	*retval = 0;
	return 0;
}

int sys_lseek(userptr_t fd, off_t seek_pos, userptr_t whence, off_t* retval) {
	int result = 0;
	*retval = -1;
	struct proc* curprocess = curproc;
	int seek_fd = (int)fd;
	int seek_whence;
	result = copyin(whence, &seek_whence, sizeof(int));
	if (result) {
		return result;
	}

	struct filetable_entry* entry = filetable_lookup(curprocess->p_filetable,
			seek_fd);
	if (entry == NULL) {
		return EBADF;
	}

	struct file_handle* handle = entry->ft_handle;
	struct vnode* file_vnode = handle->fh_vnode;

	// check if the fd id seekable
	if (!VOP_ISSEEKABLE(file_vnode)) {
		return ESPIPE;
	}

	// lock the operation
	lock_acquire(file_vnode->vn_opslock);

	// seek based on the value of whence
	off_t new_pos;
	struct stat statbuf;
	switch (seek_whence) {
	case SEEK_SET:
		new_pos = seek_pos;
		if (new_pos < 0) {
			lock_release(file_vnode->vn_opslock);
			return EINVAL;
		}
		handle->fh_offset = new_pos;
		// release lock on the vnode
		lock_release(file_vnode->vn_opslock);
		*retval = new_pos;
		return 0;

	case SEEK_CUR:
		new_pos = handle->fh_offset + seek_pos;
		if (new_pos < 0) {
			lock_release(file_vnode->vn_opslock);
			return EINVAL;
		}
		handle->fh_offset = new_pos;
		// release lock on the vnode
		lock_release(file_vnode->vn_opslock);
		*retval = new_pos;
		return 0;

	case SEEK_END:
		VOP_STAT(file_vnode, &statbuf);
		new_pos = statbuf.st_size + seek_pos;

		if (new_pos < 0) {
			lock_release(file_vnode->vn_opslock);
			return EINVAL;
		}
		handle->fh_offset = new_pos;
		// release lock on the vnode
		lock_release(file_vnode->vn_opslock);
		*retval = new_pos;
		return 0;

	default:
		// release lock on the vnode
		lock_release(file_vnode->vn_opslock);
		return EINVAL;
	}

}

int sys_dup2(userptr_t oldfd, userptr_t newfd, int32_t* retval) {

	int result = 0;
	*retval = -1;
	struct proc* curprocess = curproc;

	int k_oldfd = (int) oldfd, k_newfd = (int) newfd;

	//kprintf("TEMPPPP: %d dup2 HERE %d, %d\n", curproc->p_pid, (int)oldfd, (int)newfd);

	// look up the fds
	struct filetable_entry* oldfd_entry = filetable_lookup(
			curprocess->p_filetable, k_oldfd);
	struct filetable_entry* newfd_entry = filetable_lookup(
			curprocess->p_filetable, k_newfd);

	if (oldfd_entry == NULL) {
		kprintf("TEMPPPP: %d, dup2 entry is null for  %d, %d\n",curproc->p_pid,(int)oldfd, (int)newfd);
		return EBADF;
	}

	// if newfd is already present, destroy its file handle
	// and make it point to oldfd's file handle
	if (newfd_entry != NULL) {
		filehandle_destroy(newfd_entry->ft_handle);
		newfd_entry->ft_handle = oldfd_entry->ft_handle;
		filehandle_incref(newfd_entry->ft_handle);
		*retval = k_newfd;
		return result;
	}

	struct filetable_entry* entry = (struct filetable_entry*) kmalloc(
				sizeof(struct filetable_entry));
	entry->ft_fd = k_newfd;
	entry->ft_handle = oldfd_entry->ft_handle;
	filehandle_incref(entry->ft_handle);
	filetable_addfd(curprocess, entry);

	*retval = k_newfd;
	return result;
}

