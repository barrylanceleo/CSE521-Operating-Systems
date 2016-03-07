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
	struct vnode *file_vnode;

	// return -1 if error
	*retval = -1;

	// copy the file_name to the kernel space
	int filename_len = strlen((char*) file_name);
	char k_filename[filename_len + 1];
	if ((result = copyinstr(file_name, k_filename, filename_len + 1, 0)) != 0) {
		return result;
	}

	int k_arguments;
	result = copyin(arguments, &k_arguments, sizeof(int));

	mode_t k_mode;
	result = copyin(mode, &k_mode, sizeof(mode_t));

	// get/open vnode
	if ((result = vfs_open(k_filename, k_arguments, k_mode, &file_vnode))
			!= 0) {
		return result;
	}

	// TODO move this to filetable.c
	// create a filetable entry
	struct filetable_entry* entry = (struct filetable_entry*) kmalloc(
			sizeof(struct filetable_entry));
	entry->ft_fd = curproc->p_fdcount++;
	entry->ft_offset = 0;
	entry->ft_permission = k_arguments;
	entry->ft_vnode = file_vnode;

	// TODO move this to filetable.c
	// add the entry to the filetable
	unsigned int index;
	if ((result = array_add(curproc->p_filetable, entry, &index)) != 0) {
		return result;
	}

	// update the return value with the fd
	*retval = entry->ft_fd;

	return result;
}

int sys_read(userptr_t fd, userptr_t user_buf_ptr, userptr_t buflen,
		int32_t* retval) {

	int result = 0;
	int read_fd;
	size_t read_buflen;
	result = copyin(fd, &read_fd, sizeof(int));
	result = copyin(buflen, &read_buflen, sizeof(size_t));
	if (result) {
		*retval = -1;
		return result;
	}

	struct filetable_entry* read_ft_entry = 0;

	// TODO move this to filetable.c
	// get the file table entry for the fd
	unsigned int i;
	for (i = 0; i < array_num(curproc->p_filetable); i++) {
		read_ft_entry = (struct filetable_entry*) array_get(
				curproc->p_filetable, i);
		if (read_fd == read_ft_entry->ft_fd) {
			// check if the fd has read permission
			if ((read_ft_entry->ft_permission & O_RDONLY) == O_RDONLY
					|| read_ft_entry->ft_permission & O_RDWR) {
				break;
			} else {
				read_ft_entry = 0;
				break;
			}
		}
		read_ft_entry = 0;
	}

	if (read_ft_entry == 0) {
		// TODO decide an errno
		*retval = -1;
		return EBADF;
	}

	// lock the operation
	lock_acquire(read_ft_entry->ft_vnode->vn_opslock);

	//read the data to the uio
	struct iovec iov;
	struct uio kuio;
	void * kbuf = kmalloc(read_buflen);
	uio_kinit(&iov, &kuio, kbuf, read_buflen, read_ft_entry->ft_offset,
			UIO_READ);
	result = VOP_READ(read_ft_entry->ft_vnode, &kuio);
	if (result) {
		// release lock on the vnode
			lock_release(read_ft_entry->ft_vnode
					->vn_opslock);
		kfree(kbuf);
		*retval = -1;
		return EIO;
	}

	// copy the read data to the userspace
	result = copyoutstr(kbuf, user_buf_ptr, kuio.uio_resid, 0);
	if (result) {
		// release lock on the vnode
					lock_release(read_ft_entry->ft_vnode
							->vn_opslock);
		kfree(kbuf);
		*retval = -1;
		return EFAULT;
	}

	// update the offset in the file table
	read_ft_entry->ft_offset += kuio.uio_resid;

	// update the retval to indicate the number of bytes read
	*retval = kuio.uio_resid;

	// release lock on the vnode
				lock_release(read_ft_entry->ft_vnode
						->vn_opslock);
	kfree(kbuf);
	return result;

}

int sys_write(userptr_t fd, userptr_t user_buf_ptr, userptr_t nbytes,
		int32_t* retval) {

	int result = 0;
	int write_fd;
	ssize_t write_nbytes;
	result = copyin(fd, &write_fd, sizeof(int));
	result = copyin(nbytes, &write_nbytes, sizeof(ssize_t));
	if (result) {
		*retval = -1;
		return result;
	}
	struct filetable_entry* write_ft_entry = 0;

	// get the file table entry for the fd
	unsigned int i;
	for (i = 0; i < array_num(curproc->p_filetable); i++) {
		write_ft_entry = (struct filetable_entry*) array_get(
				curproc->p_filetable, i);
		if (write_fd == write_ft_entry->ft_fd) {
			// check if the fd has read permission
			if ((write_ft_entry->ft_permission & O_RDONLY) == O_WRONLY
					|| write_ft_entry->ft_permission & O_RDWR) {
				break;
			} else {
				write_ft_entry = 0;
				break;
			}
		}
		write_ft_entry = 0;
	}

	if (write_ft_entry == 0) {
		*retval = -1;
		return EBADF;
	}

	// lock the operation
		lock_acquire(write_ft_entry->ft_vnode->vn_opslock);

	// copy the data to the kernel space
	void *kbuf = kmalloc(write_nbytes);
	if ((result = copyinstr(user_buf_ptr, kbuf, write_nbytes, 0)) != 0) {
		// release lock on the vnode
						lock_release(write_ft_entry->ft_vnode
								->vn_opslock);
		kfree(kbuf);
		return EFAULT;
	}

	// write the data from the uio to the file
	struct iovec iov;
	struct uio kuio;
	uio_kinit(&iov, &kuio, &kbuf, write_nbytes, write_ft_entry->ft_offset,
			UIO_WRITE);
	result = VOP_WRITE(write_ft_entry->ft_vnode, &kuio);
	if (result) {
		// release lock on the vnode
						lock_release(write_ft_entry->ft_vnode
								->vn_opslock);
		kfree(kbuf);
		return result;
	}
	// update the offset in the file table
	*retval = kuio.uio_resid;
	write_ft_entry->ft_offset += kuio.uio_resid;

	// update the retval to indicate the number of bytes read
	// release lock on the vnode
					lock_release(write_ft_entry->ft_vnode
							->vn_opslock);
	kfree(kbuf);
	return result;

}

int sys_close(userptr_t fd) {

	int result = 0;
	int clos_fd;
	result = copyin(fd, &clos_fd, sizeof(ssize_t));
	if (result) {
		return result;
	}
	struct filetable_entry* close_ft_entry = 0;

	// get the file table entry for the fd
	unsigned int i;
	for (i = 0; i < array_num(curproc->p_filetable); i++) {
		close_ft_entry = (struct filetable_entry*) array_get(
				curproc->p_filetable, i);
		if (clos_fd == close_ft_entry->ft_fd) {
			// remove the entry from the filetable
			array_remove(curproc->p_filetable, i);

			// close the vnode based on the refcount
			vfs_close(close_ft_entry->ft_vnode);

			// free memory allocated for the entry
			kfree(close_ft_entry);

			return result;
		}
	}
	return EBADF;
}

int sys_lseek(userptr_t fd, userptr_t pos, userptr_t whence, int32_t* retval) {
	int result = 0;
	*retval = -1;
	int seek_fd, seek_whence;
	off_t seek_pos;
	result = copyin(fd, &seek_fd, sizeof(int));
	result = copyin(pos, &seek_pos, sizeof(off_t));
	result = copyin(whence, &seek_whence, sizeof(int));
	if (result) {
		return result;
	}

	struct filetable_entry* seek_ft_entry = 0;

	// get the file table entry for the fd
	unsigned int i;
	for (i = 0; i < array_num(curproc->p_filetable); i++) {
		seek_ft_entry = (struct filetable_entry*) array_get(
				curproc->p_filetable, i);
		if (seek_fd == seek_ft_entry->ft_fd) {
			break;
		}
		seek_ft_entry = 0;
	}

	if (seek_ft_entry == 0) {
		return EBADF;
	}

	// check if the fd id seekable
	if (!VOP_ISSEEKABLE(seek_ft_entry->ft_vnode)) {
		return ESPIPE;
	}

	// lock the operation
			lock_acquire(seek_ft_entry->ft_vnode->vn_opslock);

	// seek based on the value of whence
	off_t new_pos;
	struct stat statbuf;
	switch (seek_whence) {
	case SEEK_SET:
		new_pos = seek_pos;
		if (new_pos < 0) {
			return EINVAL;
		}
		seek_ft_entry->ft_offset = new_pos;
		// release lock on the vnode
						lock_release(seek_ft_entry->ft_vnode
								->vn_opslock);
		return new_pos;

	case SEEK_CUR:
		new_pos = seek_ft_entry->ft_offset + seek_pos;
		if (new_pos < 0) {
			return EINVAL;
		}
		seek_ft_entry->ft_offset = new_pos;
		// release lock on the vnode
						lock_release(seek_ft_entry->ft_vnode
								->vn_opslock);
		return new_pos;

	case SEEK_END:
		VOP_STAT(seek_ft_entry->ft_vnode, &statbuf);
		new_pos = statbuf.st_size + seek_pos;
		if (new_pos < 0) {
			return EINVAL;
		}
		seek_ft_entry->ft_offset = new_pos;
		// release lock on the vnode
						lock_release(seek_ft_entry->ft_vnode
								->vn_opslock);
		return new_pos;

	default:
		// release lock on the vnode
						lock_release(seek_ft_entry->ft_vnode
								->vn_opslock);
		return EINVAL;
	}

}

