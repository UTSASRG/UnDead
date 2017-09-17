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
* @file libundead.cpp
* @brief Main file
* @author Tongping Liu, <http://www.cs.utsa.edu/~tongpingliu/>
* @author Jinpeng Zhou, Jinpeng.Zhou@utsa.edu
*/
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include "xthread.hh"

#ifdef ENABLE_PREVENTION
#include "prevention.hh"
#endif
void initializer (void) __attribute__((constructor));
void finalizer (void)  __attribute__((destructor));

thread_t *threadsInfo;	// threads' data
real_thread_t *threadsInfoReal;
uintptr_t globalStackAddr;	// the lowest addr of global stack for all sub-threads
volatile int aliveThreads;	
bool isSingleThread; 
int mutexUnit;

void* mainTop;
void* textTop;

#ifdef ENABLE_PREVENTION
// real mutex region
my_mutex* realMutexStart;
my_mutex* realMutexEnd;
size_t realMutexIndex;

bool enablePrevention;
#endif

void initializer (void) {
	// wrap functions
	init_real_functions();

	// Threads data allocation
	threadsInfo = new thread_t[xdefines::MAX_THREADS];
	if(threadsInfo == NULL) {
		fprintf(stderr, "Failed to allocate threads data!\n");
		abort();
	}
	threadsInfoReal = new real_thread_t[xdefines::MAX_THREADS];

	void* ptr = MM::mmapAllocatePrivate((size_t)xdefines::STACK_SIZE * xdefines::MAX_THREADS);
	if(ptr == NULL) {
		fprintf(stderr, "Failed to allocate customized stack!\n");
		abort();
	}

	// Global initialization
	aliveThreads = 1;
	isSingleThread = true;
	globalStackAddr = (uintptr_t)ptr;

	// ONLY set value here
	mutexUnit = 64 * (sizeof(pthread_mutex_t) / 64 + 1);

#ifdef ENABLE_PREVENTION
	realMutexStart = (my_mutex*)malloc((size_t)xdefines::MAX_SYNC_OBJ * sizeof(my_mutex));
	realMutexEnd = realMutexStart + xdefines::MAX_SYNC_OBJ ;
	realMutexIndex = 0;

	enablePrevention = false;
	prevention::getInstance().initialize();
#endif

	xthread::getInstance().initialize();
	
	//fprintf(stderr, "Now we have initialized successfully!\n"); 
}

void finalizer (void) {
	xthread::getInstance().finalize();
	delete[] threadsInfo;
#ifdef ENABLE_PREVENTION
	free((void*)realMutexStart);
#endif
}

typedef int (*main_fn_t)(int, char**, char**);

extern "C" int __libc_start_main(main_fn_t, int, char**, void (*)(), void (*)(), void (*)(), void*) __attribute__((weak, alias("undead_libc_start_main")));

extern "C" int undead_libc_start_main(main_fn_t main_fn, int argc, char** argv, void (*init)(), void (*fini)(), void (*rtld_fini)(), void* stack_end) {
	//Find the real __libc_start_main
	auto real_libc_start_main = (decltype(__libc_start_main)*)dlsym(RTLD_NEXT, "__libc_start_main");
	mainTop = __builtin_return_address(0);
	return real_libc_start_main(main_fn, argc, argv, init, fini, rtld_fini, stack_end);
}

// Intercept the pthread_create function.
int pthread_create(pthread_t * tid, const pthread_attr_t * attr, void *(*start_routine) (void *), void * arg) {
	return xthread::getInstance().thread_create(tid, attr, start_routine, arg);
}

int pthread_join(pthread_t tid, void** retval) {
	return xthread::getInstance().thread_join(tid, retval);
}

int pthread_mutex_destroy (pthread_mutex_t* mutex) {
#ifdef ENABLE_PREVENTION
	pthread_mutex_t* real_mutex = (pthread_mutex_t*)getSyncEntry(mutex);
	return WRAP(pthread_mutex_destroy)(real_mutex);
#else
	return WRAP(pthread_mutex_destroy)(mutex);	
#endif
}

int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr) {
	int ret;
#ifdef ENABLE_PREVENTION
	// get thread index
	int index = getThreadIndexFromStack((uintptr_t)&mutex);
	thread_t * current = &threadsInfo[index];
	// check corresponding real_mutex
	pthread_mutex_t* real_mutex = (pthread_mutex_t*)getSyncEntry(mutex);
	if(real_mutex == mutex) {	// if there's no corresponding real _mutex
		// assign a new one
		size_t index = __atomic_fetch_add(&realMutexIndex, 1, __ATOMIC_RELAXED);
		real_mutex = (pthread_mutex_t*)(realMutexStart + index);
		setSyncEntry(mutex, real_mutex);
	}
	uintptr_t redirect = 0; // previous redirection result
#ifndef DISABLE_INIT_CHECK
	unsigned long esp;
	GET_ESP(esp);
	OffsetHashMap * offsetMap = current->initOffsetMap;
	uintptr_t offset = (unsigned long)current->stackTop - esp;
	void* combined = (void*)((offset) ^ (uintptr_t)mutex);
	OffsetInfoList* oil;

	if(!offsetMap->find(combined, 8, &oil)) {
		// new
		oil = new OffsetInfoList;
		offsetMap->insert(combined, 8, oil);
	} else {
		if(oil->hasEntry(offset, mutex, &redirect)) {
			// already exit, don't care call stacks
			// directly do redirection and return, based on previous result
			if(redirect == 0) return WRAP(pthread_mutex_init)(real_mutex, attr); 
			else *(uintptr_t*)real_mutex = (uintptr_t)redirect;
			return 0;
		}
	}
#endif
	// backtrace
	void* addr[xdefines::MAX_BACKTRACE_DEPTH] = {NULL};
	current->isRecursive = true;
	int len = backtrace(addr, xdefines::MAX_BACKTRACE_DEPTH);
	current->isRecursive = false;
	// initialize the real mutex
	if(enablePrevention) {
		ret = prevention::getInstance().mutex_init(mutex, real_mutex, attr, current, addr, len, &redirect);
	} else {
		ret = WRAP(pthread_mutex_init)(real_mutex, attr);
	}
	
#ifndef DISABLE_INIT_CHECK
	oil->insertToTail(new OffsetInfo(offset, mutex, redirect));
#endif

#ifdef ENABLE_ANALYZER
	// record call stack
	if(ret == 0 ) {
		//analyzer::getInstance().updateMutexInitCallstack((void*)real_mutex, mutex, current, addr, len);
		updateMutexInitCallstack((void*)real_mutex, mutex, current, addr, len);
	}
#endif

#else
	ret = WRAP(pthread_mutex_init)(mutex, attr);
#endif
	return ret;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
	int index = getThreadIndexFromStack((uintptr_t)&mutex);
	thread_t * current = &threadsInfo[index];
	if(current->isRecursive) return WRAP(pthread_mutex_lock)(mutex);
#ifdef ENABLE_PREVENTION
	// get corresponding real_mutex
	pthread_mutex_t* real_mutex = (pthread_mutex_t*)getSyncEntry(mutex);
	if(enablePrevention) { // or we can skip checking enablePrevention
		if(prevention::getInstance().checkInDirection(real_mutex)) {
			// this is a special lock with redirection
			pthread_mutex_t *realMutex = (pthread_mutex_t*)(*(uintptr_t*)real_mutex);
			// record
			if(!updateSpecialByLock(current, realMutex, mutex)) return 0;
			if(!isSingleThread) updateDependency(current, realMutex);	
			return WRAP(pthread_mutex_lock)(realMutex);
		} else {
			// this is not a special lock
			if(!isSingleThread) updateDependency(current, mutex);
			return WRAP(pthread_mutex_lock)(real_mutex);
		}
	} else {
		if(!isSingleThread) updateDependency(current, mutex);	
		return WRAP(pthread_mutex_lock)(real_mutex);
	}
#else
	if(!isSingleThread) updateDependency(current, mutex);	
	// Acquire the actual mutex.
	return WRAP(pthread_mutex_lock)(mutex);
#endif
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
	int ret;
	int index = getThreadIndexFromStack((uintptr_t)&mutex);
	thread_t * current = &threadsInfo[index];
	if(current->isRecursive) return WRAP(pthread_mutex_trylock)(mutex);
#ifdef ENABLE_PREVENTION
	pthread_mutex_t* real_mutex = (pthread_mutex_t*)getSyncEntry(mutex);
	if(enablePrevention) {
		if(prevention::getInstance().checkInDirection(real_mutex)) {
			pthread_mutex_t *realMutex = (pthread_mutex_t*)(*(uintptr_t*)real_mutex);
			ret = WRAP(pthread_mutex_trylock)(realMutex);
			if(ret == 0) {
				if(!updateSpecialByLock(current, realMutex, mutex)) return 0;
				if(!isSingleThread) updateDependencyByTryLock(current, realMutex);
			}
		} else {
			ret = WRAP(pthread_mutex_trylock)(real_mutex);
			if(ret == 0 && !isSingleThread) updateDependencyByTryLock(current, mutex);	
		}
	} else {
		ret = WRAP(pthread_mutex_trylock)(real_mutex);
		if(ret == 0 && !isSingleThread) updateDependencyByTryLock(current, mutex);	
	}
#else
	ret = WRAP(pthread_mutex_trylock)(mutex);
	if(ret == 0 && !isSingleThread) updateDependencyByTryLock(current, mutex);	
#endif
	return ret;
}

int pthread_mutex_unlock(pthread_mutex_t* mutex) {
	int ret;
	int index = getThreadIndexFromStack((uintptr_t)&mutex);
	thread_t * current = &threadsInfo[index];
	if(current->isRecursive) return WRAP(pthread_mutex_unlock)(mutex);;
#ifdef ENABLE_PREVENTION
	pthread_mutex_t* real_mutex = (pthread_mutex_t*)getSyncEntry(mutex);
	if(enablePrevention) {
		if(prevention::getInstance().checkInDirection(real_mutex)) {
			pthread_mutex_t *realMutex = (pthread_mutex_t*)(*(uintptr_t*)real_mutex);
			if(index != 0 && !updateSpecialByUnLock(current, realMutex, mutex)) return 0;
			ret = WRAP(pthread_mutex_unlock)(realMutex);
			if(!isSingleThread && ret == 0) updateHoldingSetByUnlock(current, realMutex);
		} else {
			ret = WRAP(pthread_mutex_unlock)(real_mutex);
			if(!isSingleThread && ret == 0) updateHoldingSetByUnlock(current, mutex);
		}
	} else {
		ret = WRAP(pthread_mutex_unlock)(real_mutex);
		if(!isSingleThread && ret == 0) updateHoldingSetByUnlock(current, mutex);
	}
#else
	ret = WRAP(pthread_mutex_unlock)(mutex);
	if(!isSingleThread && ret == 0) updateHoldingSetByUnlock(current, mutex);
#endif
	return ret;
}

int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex) {
#ifdef ENABLE_PREVENTION
	int index = getThreadIndexFromStack((uintptr_t)&mutex);
	thread_t * current = &threadsInfo[index];
	pthread_mutex_t* real_mutex = (pthread_mutex_t*)getSyncEntry(mutex);
	if(enablePrevention) {
		if(prevention::getInstance().checkInDirection(real_mutex)) {
			pthread_mutex_t* realMutex = (pthread_mutex_t*)(*(uintptr_t*)real_mutex);
			updateDependencyWithCond(current, realMutex);
			return WRAP(pthread_cond_wait)(cond, realMutex);
		} else {
			updateDependencyWithCond(current, mutex);
			return WRAP(pthread_cond_wait)(cond, real_mutex);
		}
	} else {
		updateDependencyWithCond(current, mutex);
		return WRAP(pthread_cond_wait)(cond, real_mutex);
	}
#else
	return WRAP(pthread_cond_wait)(cond, mutex);
#endif
}

int pthread_cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex, const struct timespec * abstime) {
#ifdef ENABLE_PREVENTION
	int index = getThreadIndexFromStack((uintptr_t)&mutex);
	thread_t * current = &threadsInfo[index];
	pthread_mutex_t* real_mutex = (pthread_mutex_t*)getSyncEntry(mutex);
	if(enablePrevention) {
		if(prevention::getInstance().checkInDirection(real_mutex)) {
			pthread_mutex_t* realMutex = (pthread_mutex_t*)(*(uintptr_t*)real_mutex);
			updateDependencyWithCond(current, realMutex);
			return WRAP(pthread_cond_timedwait)(cond, realMutex, abstime);
		} else {
			updateDependencyWithCond(current, mutex);
			return WRAP(pthread_cond_timedwait)(cond, real_mutex, abstime);
		}
	} else {
		updateDependencyWithCond(current, mutex);
		return WRAP(pthread_cond_timedwait)(cond, real_mutex, abstime);
	}
#else
	return WRAP(pthread_cond_timedwait)(cond, mutex, abstime);
#endif
}
