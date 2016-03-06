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
#include <kern/test161.h>
#include <spinlock.h>

/*
 * Use these stubs to test your reader-writer locks.
 */

#define NREADLOOPS    120
#define NWRITELOOPS    120
#define CREATELOOPS		8
#define NSEMLOOPS     63
#define NLOCKLOOPS    120
#define NTHREADS      32
#define SYNCHTEST_YIELDER_MAX 16
#define SUCCESS 0
#define FAIL 1

static volatile unsigned long testval1;
static volatile unsigned long testval2;
static volatile unsigned long testval3;
static volatile int32_t testval4;

static volatile int32_t test2val;
static volatile int32_t test2max;

static struct rwlock * rw_test_lock;
static struct semaphore *donesem = NULL;

static struct rwlock * rw_test_lock2;
static struct semaphore *donesem2 = NULL;

struct spinlock status_lock;
static bool test_status = FAIL;

static
bool
failif(bool condition) {
    if (condition) {
        spinlock_acquire(&status_lock);
        test_status = FAIL;
        spinlock_release(&status_lock);
    }
    return condition;
}
static
void readtestthread(void *junk, unsigned long num)
{
    (void)junk;
    (void)num;

    int i;

    unsigned int temp1, temp2, temp3;
    for(i = 0; i < NREADLOOPS; ++i) {

        //kprintf("before acquiring readlock in thread %lu\n", num);
        rwlock_acquire_read(rw_test_lock);
        //kprintf("acquired readlock in thread %lu\n", num);

        temp1 = testval1;
        temp2 = testval2;
        temp3 = testval3;
        random_yielder(4);
        if(temp1 != testval1 || temp2 != testval2 || temp3 != testval3) {
            goto fail;
        }

        //kprintf("before releasing readlock in thread %lu\n", num);
        rwlock_release_read(rw_test_lock);
        //kprintf("released readlock in thread %lu\n", num);

    }

    V(donesem);
    return;

    fail:
    rwlock_release_read(rw_test_lock);
    failif(true);
    V(donesem);
    return;
}

static
void writetestthread(void *junk, unsigned long num)
{
    (void)junk;

    int i;

    for(i = 0; i < NWRITELOOPS; ++i)
    {
        //kprintf("before acquiring writelock in thread %lu\n", num);
        rwlock_acquire_write(rw_test_lock);
        //kprintf("acquired writelock in thread %lu\n", num);

        testval1 = num;
        testval2 = num*num;
        testval3 = num%3;

        if (testval2 != testval1*testval1) {
            goto fail;
        }
        random_yielder(4);

        if (testval2%3 != (testval3*testval3)%3) {
            goto fail;
        }
        random_yielder(4);

        if (testval3 != testval1%3) {
            goto fail;
        }
        random_yielder(4);

        if (testval1 != num) {
            goto fail;
        }
        random_yielder(4);

        if (testval2 != num*num) {
            goto fail;
        }
        random_yielder(4);

        if (testval3 != num%3) {
            goto fail;
        }
        random_yielder(4);

        //kprintf("before releasing writelock in thread %lu\n", num);
        rwlock_release_write(rw_test_lock);
        //kprintf("released writelock in thread %lu\n", num);


    }

    V(donesem);
    return;

    fail:
    rwlock_release_write(rw_test_lock);
    failif(true);
    V(donesem);
    return;
}


int rwtest(int nargs, char **args) {
    (void)nargs;
    (void)args;

    int i, result;

    kprintf_n("Starting rwtest...\n");
    for (i=0; i<CREATELOOPS; i++) {
        kprintf_t(".");
        rw_test_lock = rwlock_create("testlock");

        if (rw_test_lock == NULL) {
            panic("rwtest: rwlock_create failed\n");
        }
        donesem = sem_create("donesem", 0);
        if (donesem == NULL) {
            panic("rwtest: sem_create failed\n");
        }
        if (i != CREATELOOPS - 1) {
            rwlock_destroy(rw_test_lock);
            sem_destroy(donesem);
        }
    }
    spinlock_init(&status_lock);
    test_status = SUCCESS;

    for (i=0; i<NTHREADS; i++) {
        kprintf_t(".");
        result = thread_fork("readtest", NULL, readtestthread, NULL, i);
        if (result) {
            panic("rwtest: thread_fork failed: %s\n", strerror(result));
        }
    }
    for (i=0; i<NTHREADS/2; i++) {
        kprintf_t(".");
        result = thread_fork("writetest", NULL, writetestthread, NULL, i);
        if (result) {
            panic("rwtest: thread_fork failed: %s\n", strerror(result));
        }
    }

    for (i=0; i<(NTHREADS + NTHREADS/2); i++) {
        kprintf_t(".");
        P(donesem);
    }

    rwlock_destroy(rw_test_lock);
    sem_destroy(donesem);
    donesem = NULL;

    kprintf_t("\n");
    success(test_status, SECRET, "rwt1");

    return 0;
}

static
void readtestthread2(void *junk, unsigned long num)
{
    (void)junk;
    (void)num;

    int i;

    for(i = 0; i < NREADLOOPS; ++i) {

        //kprintf("before acquiring readlock in thread %lu\n", num);
        rwlock_acquire_read(rw_test_lock2);

        test2max = test2val > test2max ? test2val : test2max;
        test2val++;
        random_yielder(4);
        test2val--;

        //kprintf("before releasing readlock in thread %lu\n", num);
        rwlock_release_read(rw_test_lock2);
        //kprintf("released readlock in thread %lu\n", num);

    }

    V(donesem2);
    return;

}

int rwtest2(int nargs, char **args) {
    (void)nargs;
    (void)args;

    int i, result;

    test2val = 0;
    test2max = 0;

    kprintf_n("Starting rwtest...\n");
    for (i=0; i<CREATELOOPS; i++) {
        kprintf_t(".");
        rw_test_lock2 = rwlock_create("testlock");

        if (rw_test_lock2 == NULL) {
            panic("rwtest: rwlock_create failed\n");
        }
        donesem2 = sem_create("donesem", 0);
        if (donesem2 == NULL) {
            panic("rwtest: sem_create failed\n");
        }
        if (i != CREATELOOPS - 1) {
            rwlock_destroy(rw_test_lock2);
            sem_destroy(donesem2);
        }
    }
    spinlock_init(&status_lock);
    test_status = SUCCESS;

    for (i=0; i<NTHREADS; i++) {
        kprintf_t(".");
        result = thread_fork("readtest", NULL, readtestthread2, NULL, i);
        if (result) {
            panic("rwtest: thread_fork failed: %s\n", strerror(result));
        }
    }

    for (i=0; i<NTHREADS; i++) {
        kprintf_t(".");
        P(donesem2);
    }

    rwlock_destroy(rw_test_lock2);
    sem_destroy(donesem2);
    donesem2 = NULL;

    kprintf_t("\n");
    if(test2max > 0) {
        success(test_status, SECRET, "rwt2");
    } else {
        success(FAIL, SECRET, "rwt2");
    }

    return 0;
}

int rwtest3(int nargs, char **args) {

    (void)nargs;
    (void)args;

    kprintf_n("Starting rwt3...\n");
    kprintf_n("(This test panics on success!)\n");

    struct rwlock * test_rwlock = rwlock_create("test_rwlock");
    if (test_rwlock == NULL) {
        panic("rwt3: rwlock_create failed\n");
    }

    //ksecprintf(SECRET, "Should panic...", "rwt3");

    rwlock_acquire_read(test_rwlock);
    rwlock_release_read(test_rwlock);
    rwlock_release_read(test_rwlock);

    /* Should not get here on success. */

    success(FAIL, SECRET, "rwt3");

    rwlock_destroy(test_rwlock);

    return 0;

}

int rwtest4(int nargs, char **args) {

    (void)nargs;
    (void)args;

    kprintf_n("Starting rwt4...\n");
    kprintf_n("(This test panics on success!)\n");

    struct rwlock * test_rwlock = rwlock_create("test_rwlock");
    if (test_rwlock == NULL) {
        panic("rwt3: rwlock_create failed\n");
    }

    // ksecprintf(SECRET, "Should panic...", "rwt4");

    rwlock_acquire_read(test_rwlock);
    rwlock_acquire_read(test_rwlock);
    rwlock_release_read(test_rwlock);
    rwlock_release_read(test_rwlock);


    rwlock_acquire_write(test_rwlock);
    rwlock_release_write(test_rwlock);
    rwlock_release_write(test_rwlock);

    /* Should not get here on success. */

    success(FAIL, SECRET, "rwt4");

    rwlock_destroy(test_rwlock);

    return 0;

}

int rwtest5(int nargs, char **args) {

    (void)nargs;
    (void)args;

    kprintf_n("Starting rwt5...\n");
    kprintf_n("(This test panics on success!)\n");

    struct rwlock * test_rwlock = rwlock_create("test_rwlock");
    if (test_rwlock == NULL) {
        panic("rwt3: rwlock_create failed\n");
    }

    // ksecprintf(SECRET, "Should panic...", "rwt5");

    rwlock_acquire_read(test_rwlock);
    rwlock_release_write(test_rwlock);

    /* Should not get here on success. */

    success(FAIL, SECRET, "rwt5");

    rwlock_destroy(test_rwlock);

    return 0;
}
