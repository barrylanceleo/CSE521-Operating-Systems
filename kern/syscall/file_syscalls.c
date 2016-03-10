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


int sys_open(userptr_t file_name, userptr_t arguments, userptr_t mode,
		int32_t* retval) {

	int result = 0;
	*retval = -1;
	struct proc* curprocess = curproc;

	// copy the file_name to the kernel space
	char k_filename[FILE_NAME_MAXLEN + 1];
	if ((result = copyinstr(file_name, k_filename, FILE_NAME_MAXLEN + 1, 0)) != 0) {
		return result;
	}

	int k_arguments;
	result = copyin(arguments, &k_arguments, sizeof(int));

	mode_t k_mode;
	result = copyin(mode, &k_mode, sizeof(mode_t));

	// insert file handle to the filetable and get the fd
	*retval = filetable_addentry(curprocess, k_filename, k_arguments, k_mode);

	return result;
}

int sys_read(userptr_t fd, userptr_t user_buf_ptr, userptr_t buflen,
		int32_t* retval) {

	int result = 0;
	*retval = -1;
	struct proc* curprocess = curproc;
	int read_fd;
	size_t read_buflen;
	result = copyin(fd, &read_fd, sizeof(int));
	result = copyin(buflen, &read_buflen, sizeof(size_t));
	if (result) {
		return result;
	}

	// get the file table entry for the fd
	 struct filetable_entry* read_ft_entry = filetable_lookup(curprocess->p_filetable, read_fd);

	if (read_ft_entry == NULL) {
		return EBADF;
	}

	struct file_handle* handle = read_ft_entry->ft_handle;
	struct vnode* file_vnode = handle->fh_vnode;

	//check if the file handle has read permission
	if ((handle->fh_permission & O_RDONLY) == O_RDONLY
						|| (handle->fh_permission & O_RDWR)){
		return EBADF;
	}

	// lock the operation
	lock_acquire(file_vnode->vn_opslock);

	//read the data to the uio
	struct iovec iov;
	struct uio kuio;
	void * kbuf = kmalloc(read_buflen);
	uio_kinit(&iov, &kuio, kbuf, read_buflen, handle->fh_offset,
			UIO_READ);
	result = VOP_READ(file_vnode, &kuio);
	if (result) {
		// release lock on the vnode
		lock_release(file_vnode->vn_opslock);
		kfree(kbuf);
		return EIO;
	}

	// copy the read data to the userspace
	result = copyout(kbuf, user_buf_ptr, kuio.uio_resid);
	if (result) {
		// release lock on the vnode
		lock_release(file_vnode->vn_opslock);
		kfree(kbuf);
		return EFAULT;
	}

	// update the offset in the file table
	handle->fh_offset += kuio.uio_resid;

	// update the retval to indicate the number of bytes read
	*retval = kuio.uio_resid;

	// release lock on the vnode
	lock_release(file_vnode->vn_opslock);

	kfree(kbuf);
	return result;

}

int sys_write(userptr_t fd, userptr_t user_buf_ptr, userptr_t nbytes,
		int32_t* retval) {

	int result = 0;
	*retval = -1;
	struct proc* curprocess = curproc;
	int write_fd;
	ssize_t write_nbytes;
	result = copyin(fd, &write_fd, sizeof(int));
	result = copyin(nbytes, &write_nbytes, sizeof(ssize_t));
	if (result) {
		return result;
	}

	// get the file table entry for the fd
	struct filetable_entry* entry = filetable_lookup(curprocess->p_filetable, write_fd);
	if (entry == NULL) {
			return EBADF;
	}

	struct file_handle* handle = entry->ft_handle;
	struct vnode* file_vnode = handle->fh_vnode;

	//check if the file handle has write has permissions
	if ((handle->fh_permission & O_WRONLY)
						|| (handle->fh_permission & O_RDWR)){
		return EBADF;
	}

	// lock the operation
	lock_acquire(file_vnode->vn_opslock);

	// copy the data to the kernel space
	void *kbuf = kmalloc(write_nbytes);
	if ((result = copyin(user_buf_ptr, kbuf, write_nbytes)) != 0) {
		// release lock on the vnode
		lock_release(file_vnode->vn_opslock);
		kfree(kbuf);
		return EFAULT;
	}

	// write the data from the uio to the file
	struct iovec iov;
	struct uio kuio;
	uio_kinit(&iov, &kuio, &kbuf, write_nbytes, handle->fh_offset,
			UIO_WRITE);
	result = VOP_WRITE(file_vnode, &kuio);
	if (result) {
		// release lock on the vnode
		lock_release(file_vnode->vn_opslock);
		kfree(kbuf);
		return result;
	}

	// update the offset in the file table
	*retval = kuio.uio_resid;
	handle->fh_offset += *retval;

	// update the retval to indicate the number of bytes read
	// release lock on the vnode
	lock_release(file_vnode->vn_opslock);
	kfree(kbuf);
	return result;
}

int sys_close(userptr_t fd, int32_t* retval) {

	int result = 0;
	*retval = -1;
	struct proc* curprocess = curproc;
	int clos_fd;
	result = copyin(fd, &clos_fd, sizeof(ssize_t));
	if (result) {
		return result;
	}

	if(filetable_remove(curprocess->p_filetable, clos_fd) == -1) {
		return EBADF;
	}

	*retval = 0;
	return 0;
}

int sys_lseek(userptr_t fd, userptr_t pos, userptr_t whence, int32_t* retval) {
	int result = 0;
	*retval = -1;
	struct proc* curprocess = curproc;
	int seek_fd, seek_whence;
	off_t seek_pos;
	result = copyin(fd, &seek_fd, sizeof(int));
	result = copyin(pos, &seek_pos, sizeof(off_t));
	result = copyin(whence, &seek_whence, sizeof(int));
	if (result) {
		return result;
	}

	struct filetable_entry* entry = filetable_lookup(curprocess->p_filetable, seek_fd);
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
			return EINVAL;
		}
		handle->fh_offset = new_pos;
		// release lock on the vnode
		lock_release(file_vnode->vn_opslock);
		return new_pos;

	case SEEK_CUR:
		new_pos = handle->fh_offset + seek_pos;
		if (new_pos < 0) {
			return EINVAL;
		}
		handle->fh_offset = new_pos;
		// release lock on the vnode
		lock_release(file_vnode->vn_opslock);
		return new_pos;

	case SEEK_END:
		VOP_STAT(file_vnode, &statbuf);
		new_pos = statbuf.st_size + seek_pos;
		if (new_pos < 0) {
			return EINVAL;
		}
		handle->fh_offset = new_pos;
		// release lock on the vnode
		lock_release(file_vnode->vn_opslock);
		return new_pos;

	default:
		// release lock on the vnode
		lock_release(file_vnode->vn_opslock);
		return EINVAL;
	}

}

int sys_dup2(userptr_t oldfd, userptr_t newfd , int32_t* retval){

	int result = 0;
	*retval = -1;
	struct proc* curprocess = curproc;

	int k_oldfd, k_newfd;
	result = copyin(oldfd, &k_oldfd, sizeof(int));
	result = copyin(newfd, &k_newfd, sizeof(int));

	// look up the fds
	struct filetable_entry* oldfd_entry = filetable_lookup(curprocess->p_filetable, k_oldfd);
	struct filetable_entry* newfd_entry = filetable_lookup(curprocess->p_filetable, k_oldfd);

	if(oldfd_entry == NULL)
	{
		return EBADF;
	}

	// if newfd is already present, destroy its file handle
	// and make it point to oldfd's file handle
	if (newfd_entry != NULL) {
		filehandle_destroy(newfd_entry->ft_handle);
		newfd_entry->ft_handle = oldfd_entry->ft_handle;
	}

	// TODO create a file table entry with the newfd
	// and make its file handle point to oldfd's file handle


	*retval = k_newfd;
	return result;
}



