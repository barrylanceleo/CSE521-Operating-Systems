/**
 * processtable.h
 *
 * Keeps track of the currently executing and zombie processes in the system.
 *
 */

#ifndef PROCESS_TABLE_H
#define PROCESS_TABLE_H
#include <types.h>
#include <proc.h>

int init_processtable(void);

int addTo_processtable(struct proc* process);

int removeFrom_processtable(int pid);

int lookup_processtable(int pid, struct proc** process);

#endif
