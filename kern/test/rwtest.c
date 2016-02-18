/*
 * All the contents of this file are overwritten during automated
 * testing. Please consider this before changing anything in this file.
 */

#include <types.h>
#include <lib.h>
#include <clock.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <kern/secret.h>
#include <spinlock.h>

#define NTHREADS 20

int rwtest(int nargs, char **args) {
	(void)nargs;
	(void)args;

	int i;
	for (i=0; i<NTHREADS; i++) {
		kprintf_t(".");
		result = thread_fork("readertest", NULL, readertestthread, NULL, i);
		if (result) {
			panic("lt1: thread_fork failed: %s\n", strerror(result));
		}
	}

	kprintf_n("rwt1 unimplemented\n");
	success(FAIL, SECRET, "rwt1");

	return 0;
}

void readertestthread() {
	//readSomeStuff;
}

void writertestthread() {
	//writeSomeStuff;
}
