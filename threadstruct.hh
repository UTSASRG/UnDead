/* Copyright (C) 
* 2017 - Jinpeng Zhou, Jinpeng.Zhou@utsa.edu
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
* 
*/
/**
* @file threadstruct.hh
* @brief Per-thread data structure and related operations
* @author Tongping Liu, <http://www.cs.utsa.edu/~tongpingliu/>
* @author Jinpeng Zhou, Jinpeng.Zhou@utsa.edu
*/
#ifndef __THREADSTRUCT_HH__
#define __THREADSTRUCT_HH__
#include <execinfo.h>

#include "xdefines.hh"

/*
 * thread_t is the thread related information
 */
typedef struct {
	int tIndex;	// thread Index
	threadFunction* startRoutine;	// thread procedure
	void* startArg;	// thread parameters
	Dependency* dependencies; // pre-allocate dependencies
	size_t depCount; // counter for depdendencies
	Dependency* curDep; // current dependency
	DependencyAddrHashMap* dependencyMap; // per thread dependency hashmap
	void** holdingSet; // current holding
	int holdingCount;
	special_holding* specialHolding; // mark special locks
	bool isRecursive; // avoid recursively intercepting
	void* stackTop; // thread's srtack top
	OffsetHashMap* offsetMap; // offset hashMap for acquisition
	OffsetHashMap* initOffsetMap; // offset hashMap for mutex init
	char align[16];
} thread_t;

typedef struct {
	Dependency* dependencies;
	size_t depCount;
	char align[48];
} real_thread_t;

extern void* textTop;
extern void* mainTop;

#ifdef ENABLE_PREVENTION
extern my_mutex* realMutexStart;
extern my_mutex* realMutexEnd;
// get corresponding real mutex
INLINE void* getSyncEntry(void* syncvar) {
	void* real = *(void**)syncvar;
	if(real > (void*)realMutexEnd || real < (void*)realMutexStart) return syncvar;
	else return real;
}

INLINE void updateMutexInitCallstack(void* mutex, void* lock, thread_t* thread, void** addr = NULL, int len = 0) {
	callstack* myStack;
	callstack* myStackList;
	myStack = new callstack();
	my_mutex* myMutex = (my_mutex*)mutex;
	myMutex->callsite = myStack;
	// record call stacks
	for(int i = 0; i < len; i++) {
		if(addr[i + 1] == mainTop || i >= xdefines::MAX_STACK_DEPTH) break;
		if(addr[i] > textTop) continue;
		myStack->stack[myStack->found++] = addr[i];
	}
}

// update dependency info when meet a cond
INLINE void updateDependencyWithCond(thread_t* thread, void* lock) {
	void** currentHolding = thread->holdingSet;
	int hc = thread->holdingCount;
	if(hc >= 2) {
		DependencyAddrHashMap* depMap = thread->dependencyMap;
		DependencyHashList* dhl;
		Dependency* dep;
		for(int i = hc - 1; i > 0; i--) {
			void* addrCombined = (void*)((uintptr_t)currentHolding[i] ^ (uintptr_t)currentHolding[i - 1]);
			if(!depMap->find((void*)addrCombined, 8, &dhl)) {
				fprintf(stderr, "Error. Not found dependencies in per-thread hash\n");
				abort();
			} else {	
				if(!dhl->hasEntry(currentHolding[i], currentHolding, i, &dep)) {
					fprintf(stderr, "Error. Not found dependencies in per-thread\n");
					abort();
				} else {
					dep->condRelated = true;
				}
			}
			// we don't have to care about previous ones.
			if(currentHolding[i] == lock) break;
		}
	}
}
#endif

// update denpendencies when there's a lock()
INLINE void updateDependency(thread_t* thread, void* lock) {
	void** currentHolding = thread->holdingSet;
	int* hc = &thread->holdingCount;
	if(*hc > 0) {
		// now we have a nested lock
		void* addrCombined = (void*)((uintptr_t)lock ^ (uintptr_t)currentHolding[*hc - 1]);
		DependencyAddrHashMap* depMap = thread->dependencyMap;
		DependencyHashList* dhl;
		Dependency* dep;
		if(!depMap->find((void*)addrCombined, 8, &dhl)) {
			// new dependency
			dep = &thread->dependencies[thread->depCount++];
#ifdef ENABLE_PREVENTION
			void* realLock = getSyncEntry(lock);
			if(realLock != lock) dep->update(lock, realLock, currentHolding, *hc);
			else dep->update(lock, 0, currentHolding, *hc);
#else
			dep->update(lock, currentHolding, *hc);
#endif
			dhl = new DependencyHashList;
			dhl->insertToTail(dep);
			depMap->insert((void*)addrCombined, 8, dhl);
		} else {
			// may exist. Further check
			if(!dhl->hasEntry(lock, currentHolding, *hc, &dep)) {
				// not exist. It's new
				dep = &thread->dependencies[thread->depCount++];
#ifdef ENABLE_PREVENTION
				void* realLock = getSyncEntry(lock);
				if(realLock != lock) dep->update(lock, realLock, currentHolding, *hc);
				else dep->update(lock, 0, currentHolding, *hc);
#else
				dep->update(lock, currentHolding, *hc);
#endif
				dhl->insertToTail(dep);
			}
		}
		// update current dependency
		thread->curDep = dep;
		// check whether need to get call stacks
		unsigned long esp;
		GET_ESP(esp);
		OffsetHashMap * offsetMap = thread->offsetMap;
		uintptr_t offset = (unsigned long)thread->stackTop - esp;
		void* combined = (void*)(offset ^ (uintptr_t)lock);
		OffsetInfoList* oil;
		if(!offsetMap->find(combined, 8, &oil)) {
			// new
			oil = new OffsetInfoList;
			offsetMap->insert(combined, 8, oil);
		} else {
			if(oil->hasEntry(offset, lock)) {
				// already exist, no need to get callstack again
				currentHolding[(*hc)++] = lock;
				return;
			}
		}
		// new one, get call stack for the 1st time
		oil->insertToTail(new OffsetInfo(offset, lock));
		// backtrace		
		void* addr[xdefines::ACQ_CALLSTACK_DEPTH]= {NULL};
		thread->isRecursive = true;
		int len = backtrace(addr, xdefines::ACQ_CALLSTACK_DEPTH);
		thread->isRecursive = false;
#if 1
		void* address[xdefines::CALLSITE_LEVEL] = {NULL};
		for(int i = 0, t = 0; i < len && t < xdefines::CALLSITE_LEVEL && addr[i + 1] != mainTop; i++) {
			if(addr[i] < textTop) address[t++] = addr[i];
		}
		dep->addNewCallsite(address[0], address[1]);
#else
		dep->addNewCallsite(addr[1], addr[2]);
#endif
	}
	currentHolding[(*hc)++] = lock;
}

#ifdef ENABLE_PREVENTION
extern int mutexUnit;

INLINE int getSpecialLockIndex(void* lock) { return ((uintptr_t)lock - ADDITIONAL_LOCK_STARTADDR) / mutexUnit; }

INLINE bool updateSpecialByLock(thread_t* thread, void* lock, void* originalLock) {
	special_holding* sl = &thread->specialHolding[getSpecialLockIndex(lock)];
	sl->updateByLock(originalLock);
	if(sl->count == 1) {
		return true; // 1st lock on the special
	}
	return false;
}

INLINE bool updateSpecialByUnLock(thread_t* thread, void* lock, void* originalLock) {
	special_holding* sl = &thread->specialHolding[getSpecialLockIndex(lock)];
	sl->updateByUnLock(originalLock);
	if(sl->count == 0) {
		return true; // last lock on the special
	}
	return false;
}
#endif

// trylocks only update holding set
INLINE void updateDependencyByTryLock(thread_t* thread, void* lock) {
	int* hc = &thread->holdingCount;
	thread->holdingSet[(*hc)++] = lock;
}

INLINE void updateHoldingSetByUnlock(thread_t* thread, void* lock) {
	void** currentHolding = thread->holdingSet;
	int* hc = &thread->holdingCount;
	int last = *hc - 1;
	for(int i = last; i >= 0; i--) {
		if(currentHolding[i] == lock) {
			for(int j = i; j < last; j++) currentHolding[j] = currentHolding[j + 1];
			(*hc)--;
			break;
		}
	}
}

extern uintptr_t globalStackAddr;

// Get the thread index by its stack address
INLINE int getThreadIndexFromStack(uintptr_t stackTop) {
	int index = (stackTop - globalStackAddr) / xdefines::STACK_SIZE;
	if (index >= xdefines::MAX_THREADS || index <= 0)
		return 0;
	return index;
}

#if defined(ENABLE_PREVENTION) && defined(ENABLE_ANALYZER)
// set up connection between mutex and real_mutex
INLINE int setSyncEntry(void* syncvar, void* realvar) {
	int ret = 0;
	uintptr_t* target = (uintptr_t*)syncvar;
	uintptr_t expected = *(uintptr_t*)target;
	if(!__atomic_compare_exchange_n(target, &expected, (uintptr_t)realvar, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
		ret = 1;
	}
	return ret;
}
#endif
#endif
