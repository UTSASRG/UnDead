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
* @file xdefines.hh
* @brief Global definations
* @author Tongping Liu, <http://www.cs.utsa.edu/~tongpingliu/>
* @author Jinpeng Zhou, Jinpeng.Zhou@utsa.edu
*/
#ifndef __XDEFINES_HH__
#define __XDEFINES_HH__

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <set>
#include <fstream>
#include <unistd.h>

#include "libfuncs.hh"
#include "hashmap.hh"
#include "hashfuncs.hh"
#include "heapallocator.hh"
#include "mm.hh"
#include "flist.hh"

using namespace std;
	#define INLINE inline __attribute__((always_inline))

	typedef void * threadFunction (void *);
	#define gettid() syscall(SYS_gettid)

  inline size_t alignup(size_t size, size_t alignto) {
    return ((size + (alignto - 1)) & ~(alignto -1));
  }
  
  inline size_t aligndown(size_t addr, size_t alignto) {
    return (addr & ~(alignto -1));
  }
  
  inline unsigned long getMin(unsigned long a, unsigned long b) {
    return (a < b ? a : b);
  }
  
  inline unsigned long getMax(unsigned long a, unsigned long b) {
    return (a > b ? a : b);
  }

class xdefines {
public:
  enum { MAX_THREADS = 1024 };
	enum { STACK_SIZE = 0x800000 }; 
	enum { MAX_SYNC_ITEMS = 4096 };
	enum { MAX_HOLDING_DEPTH = 6}; // The maximum depth of a nested lock tree
	enum { THREAD_MAP_SIZE = 1280 };
	enum { MAX_STACK_DEPTH = 5 };
	enum { MAX_BACKTRACE_DEPTH = 10 };
	enum { MAX_DEADLOCK = 4096 };
	enum { MAX_DEPENDENCY = 4096 };
	enum { MAX_SYNC_OBJ = 1000000UL};

	// for acquisition
	enum { CALLSITE_LEVEL = 2};
	enum { CALLSITE_UNIQUE_MAX = 1024};
	enum { ACQ_CALLSTACK_DEPTH = CALLSITE_LEVEL + 1};

	enum { MONITOR_PERIOD = 2 }; // monitor thread period (secs)
	enum { MONITOR_THRESHOLD = 10 }; // threadshold about when to treat it as a hung, and exit 
};

#define MAXBUFSIZE 1024
#define DEADLOCK_FILE "_deadlock.info"

#define ADDITIONAL_LOCK_STARTADDR 0x12340C000000
#define INDIRECTION_MASK ADDITIONAL_LOCK_STARTADDR

struct callstack {
	callstack() {
		found = 0;
		next = NULL;
	}
	int found;
	void* stack[xdefines::MAX_STACK_DEPTH];
	callstack* next;
	char align[8];
};

/*
 * For sets in history file
 */
struct special_info {
	special_info (void* l = NULL, callstack *cs = NULL) : lock(l), callsite(cs) {}
	void* lock;
	callstack* callsite;
};

struct special_info_list : public EntryList<special_info> {
	special_info_list() : next(NULL) {}
	special_info_list* next;
};

/*
 * For comparing call stacks
 * each path from root to leaf is a callstack
 */
struct callsite_tree {
	callsite_tree(void* addr = NULL) {
		child = NULL;
		siblings = NULL;
		callerAddr = addr;
	}

	// we re-use the callerAddr as the realMutex for leaves
	void* callerAddr; // only for intermediate nodes
	callsite_tree *child;
	callsite_tree *siblings;
};

/*
 * For writing history file
 */
typedef set<void*> MergeSet;
typedef struct merge_set_list {
	merge_set_list() : next(NULL){}
	MergeSet mergeSet;
	merge_set_list* next;
} MergeSetList;

struct deadlock_info {
	void* lock_1;
	void* lock_2;
};

/*
 * handle mutex objects
 */
struct my_mutex{
	pthread_mutex_t myMutex;
	callstack* callsite;
	char align[16];
};

/*
 * Handle counter for each special lock in a merge set.
 */
struct special_lock_list {
	special_lock_list(void* l = NULL) : lock(l), count(1) { }
	void* lock;
	int count;
};

struct special_holding : public EntryList<special_lock_list> {
	special_holding() : count(0) { }

	int count;

	void updateByLock(void* lock) {
		for(auto sll = list->next; sll != NULL; sll = sll->next) {
			if(sll->entry->lock == lock) {
				sll->entry->count++;
				count++;
				return;
			}
		}
		insertToTail(new special_lock_list(lock));
		count++;
	}

	void updateByUnLock(void* lock) {
		for(auto sll = list->next; sll != NULL; sll = sll->next) {
			if(sll->entry->lock == lock) {
				if(sll->entry->count > 0) {
					sll->entry->count--;
					if(count > 0) count--;
				}
				return;
			}
		}
	}
};

/*
 * Depedency for detection
 */
struct Dependency {
	Dependency() { }
	Dependency(void* l, void** hs, int len) {
		lock = l;
		holdingCount = len;
		for(int i = 0; i < len; i++) holdingSet[i] = hs[i];
		callsiteCount = 0;
	}
	void* lock;
	void* holdingSet[xdefines::MAX_HOLDING_DEPTH];
	int		holdingCount;

	void* callerAddr[xdefines::CALLSITE_UNIQUE_MAX][xdefines::CALLSITE_LEVEL];
	int callsiteCount;

#ifdef ENABLE_PREVENTION
	Dependency(void* l, void* real, void** hs, int len, bool cr = false) {
		lock = l;
		realLock = real;
		holdingCount = len;
		condRelated = cr;
		for(int i = 0; i < len; i++) holdingSet[i] = hs[i];
	}
	void* realLock;
	bool	condRelated;

	void update(void* l, void*real, void** hs, int len) {
		lock = l;
		realLock = real;
		holdingCount = len;
		for(int i = 0; i < len; i++) holdingSet[i] = hs[i];
	}
#endif
	
	void update(void* l, void** hs, int len) {
		lock = l;
		holdingCount = len;
		for(int i = 0; i < len; i++) holdingSet[i] = hs[i];
	}

	void addNewCallsite(void* addr_1, void* addr_2) {
		int i = 0;
		while(i < callsiteCount) {
			if(callerAddr[i][0] == addr_1 && callerAddr[i][1] == addr_2) return;
			i++;
		}
		//assert(i < xdefines::CALLSITE_UNIQUE_MAX);
		callerAddr[i][0] = addr_1;
		callerAddr[i][1] = addr_2;
		callsiteCount++;
	}
};

/*
 * Dependency List for dependency hash
 */
struct DependencyHashList : public EntryList<Dependency> {
	bool hasEntry(void* lock, void** holding, int len, Dependency** theDep) {
		for(auto * dl = list->next; dl != NULL; dl = dl->next) {
			Dependency* dep = dl->entry;
			if(dep->lock == lock && dep->holdingCount == len) {
				int i = 0;
				while(dep->holdingSet[i] == holding[i] && ++i < len);
				if(i == len) {
					*theDep = dep;
					return true;
				}
			}
		}
		return false;
	}
};
typedef HashMap<void*, DependencyHashList*, HeapAllocator> DependencyAddrHashMap;

/*
 * For chain stack for detection
 */
struct ChainList {
	ChainList(Dependency* dep = NULL, int i = -1) : depEntry(dep), tIndex(i),  prev(NULL), next(NULL) { }
	Dependency* depEntry;
	int tIndex;
	ChainList *prev;
	ChainList *next;
};

struct ChainStack {
	ChainStack() {
		list = new ChainList();
		tail = list;
	}
	ChainList* list;
	ChainList* tail;
	
	void push(Dependency* dep, int tIndex = -1) {
		ChainList* cl = new ChainList(dep, tIndex);
		tail->next = cl;
		cl->prev = tail;
		tail = cl;
	}

	void pop() {
		if(tail != list) {
			tail->prev->next = tail->next;
			ChainList* t = tail;
			tail = tail->prev;
			delete t;
		}
	}
};

struct OffsetInfo {
	OffsetInfo(uintptr_t o = 0, void* l = NULL, uintptr_t rn = 0) : offset(o), lock(l), redirect(rn) {}
	uintptr_t offset;
	void* lock;

	uintptr_t redirect;
};

struct OffsetInfoList : public EntryList<OffsetInfo> {
	bool hasEntry(uintptr_t offset, void* lock) {
		for(auto iter = list->next; iter!= NULL; iter = iter->next) {
			if(iter->entry->offset == offset && iter->entry->lock == lock) return true;			
		}
		return false;
	}

	bool hasEntry(uintptr_t offset, void* lock, uintptr_t* r) {
		for(auto iter = list->next; iter!= NULL; iter = iter->next) {
			if(iter->entry->offset == offset && iter->entry->lock == lock) {
				*r = iter->entry->redirect;
				return true;
			}
		}
		return false;
	}
};

typedef HashMap<void*, OffsetInfoList*, HeapAllocator> OffsetHashMap;

#if defined(X86_32BIT)
#define GET_ESP(x) \
{ \
		asm volatile("movl %%esp,%0\n" \
				: "=r"(x)); \
}
#else
#define GET_ESP(x) \
{ \
		asm volatile("movq %%rsp,%0\n" \
				: "=r"(x)); \
}
#endif

#endif
