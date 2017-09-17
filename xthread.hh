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
* @file xthread.hh
* @brief Functions to handle thread-related operations
* @author Tongping Liu, <http://www.cs.utsa.edu/~tongpingliu/>
* @author Jinpeng Zhou, Jinpeng.Zhou@utsa.edu
*/
#ifndef __XTHREAD_HH__
#define __XTHREAD_HH__

#include "threadstruct.hh"
#include "selfmap.hh"

#ifdef ENABLE_ANALYZER
#include "analyzer.hh"
#endif
#ifdef EBANBLE_PREVENTION
#include "prevention.hh"
#endif
extern thread_t *threadsInfo;
extern real_thread_t *threadsInfoReal;
extern uintptr_t globalStackAddr;
extern volatile int aliveThreads;
extern bool isSingleThread;
extern void* textTop;

class xthread {
private:
	xthread() { }
	
public:
	static xthread& getInstance() {
		static char buf[sizeof(xthread)];
		static xthread * theOneTrueObject = new (buf) xthread();
		return *theOneTrueObject;
	}

	/// @brief Initialize the system.
	void initialize()	{
#ifdef ENABLE_ANALYZER
		analyzer::getInstance().initialize();
#endif
		WRAP(pthread_mutex_init)(&_gMutex, NULL);

		// Initialze the Main thread
		thread_t* current = getThreadInfoByIndex(0);
		current->tIndex = 0;
		current->startRoutine = 0;

		initializeRecord(current);
		selfmap::getInstance().getTop(&current->stackTop, &textTop);
		_threadIndex = 1;
		_threadIndexReal = 0;
		_monitor = 0;

		installSignalHandler();

		// initialize the <pthread_t, thread index> map.
		_xmap.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, xdefines::THREAD_MAP_SIZE);
		_xmap.insert((void*)pthread_self(), sizeof(void*), 0);
	}

	// The end of system. 
	void finalize(void) {
#ifndef RUNTIME_OVERHEAD
#ifdef ENABLE_ANALYZER
#ifdef MONITOR_THREAD
		if(_monitor > 0) pthread_kill(_monitor, 0);
#endif
		fprintf(stderr, "start analyzing..\n");
		for(int i = 0; i < _threadIndex; i++) {
			if(threadsInfo[i].tIndex >= 0) {
				threadsInfoReal[_threadIndexReal].dependencies = threadsInfo[i].dependencies;
				threadsInfoReal[_threadIndexReal++].depCount = threadsInfo[i].depCount;
			}
		}
		analyzer::getInstance().finalize(_threadIndexReal);
#endif
#endif
	}

	void installSignalHandler() {
		struct sigaction siga;
		// Point to the handler function.
		siga.sa_flags = SA_RESTART | SA_NODEFER;
		siga.sa_handler = sigHandler;

		if (sigaction(SIGINT, &siga, NULL) == -1) {
			perror ("installing SIGINT failed\n");
			exit (-1);
		}
		if (sigaction(SIGQUIT, &siga, NULL) == -1) {
			perror ("installing SIGQUIT failed\n");
			exit (-1);
		}
		if (sigaction(SIGHUP, &siga, NULL) == -1) {
			perror ("installing SIGHUP failed\n");
			exit (-1);
		}
		if (sigaction(SIGTERM, &siga, NULL) == -1) {
			perror ("installing SIGTERM failed\n");
			exit (-1);
		}
#ifdef USING_SIGUSR1
		if (sigaction(SIGUSR1, &siga, NULL) == -1) {
			perror ("installing SIGUSR1 failed\n");
			exit (-1);
		}
#endif
#ifdef USING_SIGUSR2
		if (sigaction(SIGUSR2, &siga, NULL) == -1) {
			perror ("installing SIGUSR2 failed\n");
			exit (-1);
		}
#endif
	}

	static void sigHandler(int signum) {
		if(signum == SIGINT) {
			fprintf(stderr, "Recieved SIGINT, Genearting Report:\n");
			exit(0);
		} else if (signum == SIGQUIT) {
			fprintf(stderr, "Recieved SIGQUIT, Generating Report:\n");
			exit(0);
		} else if (signum == SIGHUP) {
			fprintf(stderr, "Recieved SIGHUP, Generating Report:\n");
			exit(0);
		} else if (signum == SIGTERM) {
			fprintf(stderr, "Recieved SIGTERM, Generating Report:\n");
			exit(0);
		} else if (signum == SIGUSR1) {
			fprintf(stderr, "Recieved SIGUSR1, Generating Report:\n");
			exit(0);
		} else if (signum == SIGUSR2) {
			fprintf(stderr, "Recieved SIGUSR2, Generating Report:\n");
			exit(0);
		}
	}

	// initialize the thread related data
	INLINE static void initializeRecord(thread_t* thread) {
		thread->dependencies = new Dependency[xdefines::MAX_DEPENDENCY];
		thread->curDep = NULL;
		thread->depCount = 0;
		thread->isRecursive = false;
		if(thread->holdingSet == NULL) {
			// initialize for the 1st time
			thread->holdingSet = new void*[xdefines::MAX_HOLDING_DEPTH];		
			thread->dependencyMap = new DependencyAddrHashMap;
			thread->dependencyMap->initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, xdefines::MAX_DEPENDENCY);
			thread->offsetMap = new OffsetHashMap;
			thread->offsetMap->initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, xdefines::MAX_DEPENDENCY);
			thread->initOffsetMap = new OffsetHashMap;
			thread->initOffsetMap->initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, xdefines::MAX_DEPENDENCY);
		} else {
			// clear old for re-use
			for(DependencyAddrHashMap::iterator iter = thread->dependencyMap->begin(); iter != thread->dependencyMap->end(); iter++) {
				thread->dependencyMap->erase(iter.getkey(), 8);
			}
			for(OffsetHashMap::iterator iter = thread->offsetMap->begin(); iter != thread->offsetMap->end(); iter++) {
				thread->offsetMap->erase(iter.getkey(), 8);
			}
			for(OffsetHashMap::iterator iter = thread->initOffsetMap->begin(); iter != thread->offsetMap->end(); iter++) {
				thread->initOffsetMap->erase(iter.getkey(), 8);
			}
		}
		thread->holdingCount = 0;

#ifdef ENABLE_PREVENTION
		if(thread->specialHolding == NULL) {
			thread->specialHolding = new special_holding[prevention::getInstance().getMergeSetAmount()];
		}
#endif
	}

	INLINE thread_t * getThreadInfoByIndex(int index){
		//assert(index < xdefines::MAX_THREADS);
    return &threadsInfo[index];
  }

	/// @ Intercepting the thread_creation operation.
	int thread_create(pthread_t * tid, const pthread_attr_t * attr, threadFunction * fn, void * arg) {
		int tindex;

		// Protect the allocation of thread index.
		global_lock();
		// Allocate a global thread index for current thread.
		if(aliveThreads++ < _threadIndex) {
			for(int i = 0; i < _threadIndex; i++) {
				if(threadsInfo[i].tIndex < 0) {
					// find the available slot
					tindex = threadsInfo[i].tIndex = i;
					break;
				}
			}
		} else {
			tindex = _threadIndex++;
		}
#if (!defined RUNTIME_OVERHEAD && defined ENABLE_ANALYZER && defined MONITOR_THREAD)
		if(_monitor == 0) {
			WRAP(pthread_create)(&_monitor, NULL, monitorThread, threadsInfo);
		}
#endif
		global_unlock();

		thread_t * children = getThreadInfoByIndex(tindex);
		children->tIndex = tindex;
		children->startRoutine = fn;
		children->startArg = arg;
		uintptr_t start = globalStackAddr + (uintptr_t)tindex * xdefines::STACK_SIZE;
		children->stackTop = (void*)(start + xdefines::STACK_SIZE);
		// modify the  stack:  lowest addr and size
		pthread_attr_t iattr;
		if(attr == NULL) {
			pthread_attr_init(&iattr);
		} else {
			iattr = *attr;
		}
		pthread_attr_setstack(&iattr, (void*)start, xdefines::STACK_SIZE);
		
		int ret = WRAP(pthread_create)(tid, &iattr, startThread, (void*)children);
		// after real creation
		global_lock();
		if(ret == 0) {
			_xmap.insertIfAbsent((void*)*tid, sizeof(void*), tindex);
		} else {
			aliveThreads--;
			children->tIndex = -1;
		}
		global_unlock();
		return ret;
  }      

	static void* startThread(void* arg) {
		thread_t* current = (thread_t*)arg;
		isSingleThread = false;	
		initializeRecord(current);		
		void* result = current->startRoutine(current->startArg);
		return result;
	}

#ifdef ENABLE_ANALYZER
	INLINE static void checkNew(thread_t* threads, void** lastHolding, int threadIndex, bool* sthNew, int* candidate) {
		for(int i = 0; i < threadIndex; i++) {
			thread_t* thread = &threads[i];
			int holds = thread->holdingCount - 1;
			// check holding status
			if(holds >= 0 && lastHolding[i] != thread->holdingSet[holds]) {
				lastHolding[i] = thread->holdingSet[holds];
				*sthNew = true; 
				if(holds > 0) (*candidate)++;
			} else if (holds < 0 && lastHolding[i] != NULL) {
				lastHolding[i] = NULL;
				*sthNew = true;
			}
		}
	}

	static void* monitorThread(void* arg) {
		thread_t* threads = (thread_t*)arg;
		// current status 
		void* lastHolding[xdefines::MAX_THREADS] = {NULL};
		int notRunning = 0;
		bool hasCycle = false;
		ChainStack* stack = new ChainStack;
		while(1) {
			// sleep
			sleep(xdefines::MONITOR_PERIOD);
			if(aliveThreads < 2) continue; // if single thread, do nothing
			int threadIndex = xthread::getInstance().getThreadIndex();
			int candidate = 0; // how many threads are holding locks
			// check whether there's something new in all threads status
			bool sthNew = false;
			checkNew(threads, lastHolding, threadIndex, &sthNew, &candidate);
			for(int i = 0; i < threadIndex; i++) {
				thread_t* thread = &threads[i];
				int holds = thread->holdingCount - 1;
				// check holding status
				if(holds >= 0 && lastHolding[i] != thread->holdingSet[holds]) {
					lastHolding[i] = thread->holdingSet[holds];
					sthNew = true; 
					if(holds > 0) candidate++;
				} else if (holds < 0 && lastHolding[i] != NULL) {
					lastHolding[i] = NULL;
					sthNew = true;
				}
			}
#if 0
			if(!sthNew) {	// nothing new
				if(hasCycle && notRunning++ > xdefines::MONITOR_THRESHOLD) {
					fprintf(stderr, "Monitor thread found cycles and nothing new happend during THRESHOLD. Now exit!\n");
					exit(0);
				}
				continue;
			}
			notRunning = 0;
#else
			if(!sthNew) continue;
			if(candidate > 1) { // check cycles if at least 2 threads
				// check current status and terminate if confirm a deadlock
				analyzer::getInstance().analysisCurrent(threadIndex, stack, lastHolding);
			}
#endif
		}
		delete stack;
	}
#endif

	int thread_join(pthread_t tid, void** retval) {
		int ret = WRAP(pthread_join)(tid, retval);
		if(ret == 0) {
			int joinee = -1;
			// update after join
			global_lock();
			if(!_xmap.find((void*)tid, sizeof(void*), &joinee)) {
				fprintf(stderr, "Cannot find joinee index for thread %p\n", (void*)tid);
			} else {
				thread_t* joineeThread = &threadsInfo[joinee];
				joineeThread->tIndex = -1; // for re-use
				// save info
				threadsInfoReal[_threadIndexReal].dependencies =  joineeThread->dependencies;
				threadsInfoReal[_threadIndexReal++].depCount =  joineeThread->depCount;
			}
			if(--aliveThreads <= 1) isSingleThread = true;
			global_unlock();
		}
		return ret;
	}

	INLINE int getThreadIndex() { return _threadIndex; }

private:
	pthread_t _monitor;
	volatile int _threadIndex; // each thread has an index
	volatile int _threadIndexReal; // for detection
	typedef HashMap<void*, int, HeapAllocator> threadHashMap;
	threadHashMap _xmap; // The hash map that map the address of pthread_t to thread index.
	pthread_mutex_t _gMutex; // mutex lock to protect thread index
	void global_lock(){ WRAP(pthread_mutex_lock)(&_gMutex); }
	void global_unlock(){ WRAP(pthread_mutex_unlock)(&_gMutex); }
};
#endif

