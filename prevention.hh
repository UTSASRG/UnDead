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
* @file prevention.hh
* @brief Provide automatic prevention.
* @author Tongping Liu, <http://www.cs.utsa.edu/~tongpingliu/>
* @author Jinpeng Zhou, Jinpeng.Zhou@utsa.edu
*/
#ifndef __PREVENTION_HH__
#define __PREVENTION_HH__

#include "xdefines.hh"

using namespace std;

extern bool enablePrevention;
extern int mutexUnit;
extern void* mainTop;
extern void* textTop;
extern char *__progname_full;

class prevention {
private:
	prevention() { }
	
public:
	static prevention& getInstance() {
		static char buf[sizeof(prevention)];
		static prevention * theOneTrueObject = new (buf) prevention();
		return *theOneTrueObject;
	}

	// read deadlock history file, and determine whether we need to enable prevention
	bool loadDeadlockInfo() {
		specialTail = specialList = new special_info_list;
		_callsiteTree = NULL;
		string deadlockFilename = string(__progname_full) + DEADLOCK_FILE;
		_deadlockFile.open(deadlockFilename.c_str());
		if(!_deadlockFile.is_open()) {
			//fprintf(stderr, "No deadlock data in %s\n", deadlockFilename.c_str());
			return false;
		}
		if(_deadlockFile.eof()) {
			_deadlockFile.close();
			return false;
		}
		// there's a hitory
		uintptr_t realMutex;
		_callsiteTree = new callsite_tree();
		while(!_deadlockFile.eof()) {
			// start read
			callsite_tree* currentNode = _callsiteTree;
			char buf;
			buf = _deadlockFile.get();
			if(buf == '-') {
				// new deadlock
				specialTail->next = new special_info_list;
				specialTail = specialTail->next;
				// assign a new special/shared lock to this new deadlock
				realMutex = (uintptr_t)ADDITIONAL_LOCK_STARTADDR + _mutexUnit * _mergesetAmount++;
				// skip the '\n'
				_deadlockFile.get();
			} else if (buf == ' ') {
				// reading detailed lock info in the same deadlock
				uintptr_t callerAddr = 0;
				uintptr_t lockAddr = 0;
				callstack* stackList = new callstack(); // record the callstacks for the lock object 
				currentNode = _callsiteTree; // current position when searching insert point in the tree
				_deadlockFile>>lockAddr; // read lock address
				while(1) { // read call stacks for this lock object
					buf = _deadlockFile.get();
					if(buf == '.') { // end of call stacks
						_deadlockFile.get();
						break;
					}
					_deadlockFile>>callerAddr; // read one caller address
					stackList->stack[stackList->found++] = (void*)callerAddr;
					// insert into tree
					callsite_tree *newNode = new callsite_tree((void*)callerAddr);
					// seach insert point
					if(currentNode->child == NULL) {
						currentNode->child = newNode;
					} else { // current node has some children. check them
						callsite_tree *t = currentNode->child;
						callsite_tree *tPrev = t;
						while(t != NULL) { // check every child
							if(t->callerAddr == (void*)callerAddr) {
								// this addr is in one child of current
								break;
							}
							tPrev = t;
							t = t->siblings;
						}
						if(t == NULL) { // this addr is a not a child of current
							tPrev->siblings = newNode;
						} else {
							currentNode = t;
							continue;
						}
					}
					currentNode = newNode;
				}
				// here we should notice that: one lock only appears once in the history file
				// record this lock object and its callstacks
				special_info* newinfo = new special_info((void*)lockAddr, stackList);
				specialTail->insertToTail(newinfo);

				if(callerAddr == 0) {
					// this lock has no call site, which means this lock is initilized by MACRO
					// this lockAddr must be a global mutex that already exists in memory
					// do in-direction here
					*(uintptr_t*)lockAddr = realMutex;
				} else if(currentNode->child == NULL){
					// this lock is initialzied by init()
					// the corresponding callstacks has not been recorded before
					// Add the shared lock as a leaf for this 'path'
					callsite_tree *newLeaf = new callsite_tree((void*)realMutex);
					currentNode->child = newLeaf;
				}
			}
		}
		_deadlockFile.close();
		if(_mergesetAmount == 0) return false;
		return true;
	}

	/// @brief Initialize the system.
	void initialize()	{
		_mergesetAmount = 0;
		_mutexUnit = mutexUnit;
		_additionalLockAddr = ADDITIONAL_LOCK_STARTADDR;
		_additionalLockAddrEnd = 0;
		// read deadlock history file and check whether enable prevention
		enablePrevention = loadDeadlockInfo();
		if(enablePrevention) {
			// allocation for additional locks
			_additionalLockAddr = (uintptr_t)MM::mmapAllocatePrivate(_mergesetAmount * _mutexUnit, false, (void*)ADDITIONAL_LOCK_STARTADDR);
			if(_additionalLockAddr == 0) {
				fprintf(stderr, "Failed to allocate for the additional locks from %p\n", (void*)ADDITIONAL_LOCK_STARTADDR);
				abort();
			} else {
				_additionalLockAddrEnd = _additionalLockAddr + _mergesetAmount * _mutexUnit;
				pthread_mutexattr_t attr;
				pthread_mutexattr_init(&attr);
				for(int index = 0; index < _mergesetAmount; index++) {
					pthread_mutex_t* mutex = (pthread_mutex_t*)(_additionalLockAddr + index * _mutexUnit);
					int ret = WRAP(pthread_mutex_init)(mutex, &attr);
					if(ret != 0) {
						fprintf(stderr, "Failed to initalize additional locks\n");
					}
				}
			}
		}
	}

	// find the addr in the tree
	callsite_tree* locateInCallsiteTree(callsite_tree* root, void* addr) {
		if(root->child == NULL) return NULL;
		for(callsite_tree *t = root->child; t != NULL; t = t->siblings) {
			if(t->callerAddr == addr) return t;
		}
		return NULL;
	}

	INLINE int mutex_init(pthread_mutex_t* mutex, pthread_mutex_t* real_mutex, const pthread_mutexattr_t* attr, thread_t* thread, void** addr, int len, uintptr_t* redirect) {
		callsite_tree *currentNode = _callsiteTree;
		for(int i = 0; i < len; i++) {
			if(addr[i + 1] == mainTop || i >= xdefines::MAX_STACK_DEPTH) break;
			if(addr[i] > textTop) continue;
			// find the caller-address's position in the tree
			currentNode = locateInCallsiteTree(currentNode, addr[i]);
			if(currentNode == NULL) {
				// no need to continue match
				break;
			}		
		}
		if(currentNode == NULL || currentNode == _callsiteTree) {
			// cannot find a corresponding node, this is a nomarl lock
			return WRAP(pthread_mutex_init)(real_mutex, attr);
		} else {
			// now currentNode's child should be a leaf in the callsite tree, we can do in-direction
			assert(currentNode->child != NULL); // should have a leaf
			*redirect = *(uintptr_t*)real_mutex = (uintptr_t)currentNode->child->callerAddr; // since it's a leaf now, the callerAddr represents the real mutex
			return 0;
		}
	}

	INLINE bool checkInDirection(void* mutex) {
		if((INDIRECTION_MASK & *(uintptr_t*)mutex) != INDIRECTION_MASK) return false;
		else return true;
	}
	
	int getMergeSetAmount() { return _mergesetAmount; }

private:
	ifstream _deadlockFile;
	size_t _mutexUnit;
	// for locks initialized by init(), record the call site of init()
	callsite_tree *_callsiteTree;	
	int _mergesetAmount;
	uintptr_t _additionalLockAddr;
	uintptr_t _additionalLockAddrEnd;

public:
	special_info_list* specialList; // list of merge sets in deadlock history file
	special_info_list* specialTail;
};
#endif
