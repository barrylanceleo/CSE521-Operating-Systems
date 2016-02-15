/*
 * Copyright (c) 2001, 2002, 2009
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
 * Driver code is in kern/tests/synchprobs.c We will
 * replace that file. This file is yours to modify as you see fit.
 *
 * You should implement your solution to the whalemating problem below.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

static struct semaphore *male_sem;
static struct semaphore *female_sem;
static struct cv *male_cv;
static struct cv *female_cv;
static struct lock *cv_lock;

/*
 * Called by the driver during initialization.
 */

void whalemating_init() {

    kprintf("in whalemating_init\n");
    cv_lock = lock_create("cv_lock");

    if(cv_lock == NULL) {
        panic("Unable to create lock");
        return;
    }

	male_sem = sem_create("male_sem", 0);
    female_sem = sem_create("female_sem", 0);

    male_cv = cv_create("male_cv");
    female_cv = cv_create("female_cv");

    if(male_sem == NULL || female_sem == NULL || male_cv == NULL || female_cv == NULL) {
        panic("Unable to create 1 or more semaphores");
        return;
    }

    return;
}

/*
 * Called by the driver during teardown.
 */

void
whalemating_cleanup() {


    sem_destroy(male_sem);
    sem_destroy(female_sem);
    cv_destroy(male_cv);
    cv_destroy(female_cv);
    lock_destroy(cv_lock);

    return;
}

void
male(uint32_t index)
{
    male_start(index); // we got a female and match maker, lets start YO!
    //kprintf("in male %u\n", index);
    V(male_sem); // notify female and match makers

    lock_acquire(cv_lock);
    cv_wait(male_cv, cv_lock); //wait till called by match maker
    lock_release(cv_lock);


    male_end(index); //done

	return;
}

void
female(uint32_t index)
{
    female_start(index); // we got a male and match maker, lets start YO!
    //kprintf("in female %u\n", index);

    V(female_sem); // notify male and match makers

    lock_acquire(cv_lock);
    cv_wait(female_cv, cv_lock); //wait till called by match maker
    lock_release(cv_lock);


    female_end(index); //done
	return;
}

void
matchmaker(uint32_t index)
{
    matchmaker_start(index);
    //kprintf("in matchmaker %u\n", index);

    P(female_sem); // wait for a female
    P(male_sem); // wait for a match maker

    lock_acquire(cv_lock);
    cv_signal(male_cv, cv_lock); // wake up a make
    cv_signal(female_cv, cv_lock); // wake up a female
    lock_release(cv_lock);


    matchmaker_end(index);

	return;
}
