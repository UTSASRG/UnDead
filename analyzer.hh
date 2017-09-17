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
* @file analyzer.hh
* @brief Provide detection for monitor thread and program-exit
* @author Tongping Liu, <http://www.cs.utsa.edu/~tongpingliu/> 
* @author Jinpeng Zhou, Jinpeng.Zhou@utsa.edu
*/
#ifndef __ANALYZER_HH__
#define __ANALYZER_HH__

#include "xdefines.hh"
#include "threadstruct.hh"

#include <vector>
#include <algorithm>

using namespace std;

#ifdef ENABLE_PREVENTION
#include "prevention.hh"
#endif

#define PREV_INSTRUCTION_OFFSET 1
extern real_thread_t *threadsInfoReal;
extern thread_t *threadsInfo;

extern char *__progname_full;
extern void* mainTop;
extern void* textTop;

class analyzer {
public:
	analyzer() { }

	static analyzer& getInstance() {
		static char buf[sizeof(analyzer)];
		static analyzer* theOneTrueObject = new (buf) analyzer();
		return *theOneTrueObject;
	}

	void initialize() {
#ifdef ENABLE_PREVENTION
		_mergeSetList = new MergeSetList();
		_mergeSetList->next = NULL;
		_mergeSetTail = _mergeSetList;
		_deadlockReported = 0;
#endif
	}

	void finalize(int threadIndex) {
		if(threadIndex > 1) {	
			_threadIndex = threadIndex;
			analysis();
		}
#ifdef REPORTFILE
		_reportFile.close();
#endif
	}

	bool analysisCurrent(int threadIndex, ChainStack* stack, void** lastHolding) {
		bool ret = false;
		//if(threadIndex < 2) return ret;
		bool isTraversed[threadIndex];
		memset(isTraversed, 0, threadIndex * sizeof(bool));
		for(int t = 0; t < threadIndex - 1; t++) {
			thread_t* thread = &threadsInfo[t];
			if(thread->curDep == NULL || thread->tIndex < 0) continue;
			isTraversed[t] = true;
			stack->push(thread->curDep, t);
			ret |= dfsCurrent(stack, t, isTraversed, threadIndex, lastHolding);
			stack->pop();
			thread->curDep = NULL;
		}
		return ret;
	}

	// dfs on current dependency
	bool dfsCurrent(ChainStack* stack, int visiting, bool* isTraversed, int threadIndex, void** lastHolding) {
		bool ret = false;
		for(int t = visiting + 1; t < threadIndex; t++) {
			thread_t* thread = &threadsInfo[t];
			if(thread->curDep == NULL || thread->tIndex < 0) continue;
			if(!isTraversed[t]) {
				Dependency* dep = thread->curDep;
				if(isChain(stack, dep)) {
					if(isCycleChain(stack, dep)) {
						ret |= true;
						// found cycles in current status, further confirm
						stack->push(dep, t); // complete the whole cycle in chain
						bool sthNew = false;
						for(ChainList* cl = stack->list->next; cl != NULL; cl = cl->next) {
							thread_t* threadInChain = &threadsInfo[cl->tIndex];
							int holds = threadInChain->holdingCount - 1;
							// check holding status
							if((holds >= 0 && lastHolding[cl->tIndex] != threadInChain->holdingSet[holds])
									|| (holds < 0 && lastHolding[cl->tIndex] != NULL)) {
								// something changed, not a real deadlock
								sthNew = true;
								break;
							}
						}
						if(!sthNew) {	// nothing changed in cycled threads, deadlock confirmed
							reportDeadlockCurrent(stack);
							fprintf(stderr, "\nMonitor thread found cycles in current status! Now exit!\n");
							exit(0);
						}
						stack->pop(); // remove the cycled one
					} else {
						isTraversed[threadIndex] = true;
						stack->push(dep, threadIndex);
						ret |= dfsCurrent(stack, visiting, isTraversed, threadIndex, lastHolding);
						stack->pop();
						isTraversed[threadIndex] = false;
					}
				}
			}
		}
		return ret;
	}

	void analysis() {
		// create output files
		fprintf(stderr, "Create output files with %s\n", __progname_full);
		char pidBuf[10];
		sprintf(pidBuf, "%d", getpid());
		string pid = string(pidBuf);
#ifdef REPORTFILE
		string reportFilename = string(__progname_full) + "_" + pid + ".report";
		_reportFile.open(reportFilename.c_str(), ios::trunc);
#endif
#ifdef ENABLE_LOG
		string logFilename = string(__progname_full) + "_" + pid + ".synclog";
		_logFile.open(logFilename.c_str(), ios::trunc);
#endif
		// if unique denpendencies < 2
		if(precheck() < 2) return;
#ifdef ENABLE_PREVENTION
		string deadlockFilename =  string(__progname_full) + DEADLOCK_FILE;
		_deadlockFile.open(deadlockFilename.c_str(), ios::trunc);
		// perform detection
		detect();
		// update prevention
		generateMergeSetInfo();
		_deadlockFile.close();
#endif
	}

	// output sync events sequence into .log file
	// return amount of unique dependencies
	size_t precheck() {
		size_t depCount = 0;
		//size_t depAmount = 0;
		char dependencyString[MAXBUFSIZE];
		Dependency* depGlobal;
		_dependencyMap.initialize(HashFuncs::hashString, HashFuncs::compareString, xdefines::MAX_DEPENDENCY);
		for(int i = 0; i < _threadIndex; i++) {	// traverse all per-thread records
			real_thread_t* current = &threadsInfoReal[i];
			//depAmount += current->depCount;
			//fprintf(stderr, "thread #%d has recorded %zu sync events\n", i, current->currentPoint);
#ifdef ENABLE_LOG
			_logFile<<"thread #"<<i<<" has recorded "<<current->depCount<<" dependencies"<<endl;
#endif
			for(size_t j = 0; j < current->depCount; j++) {
				Dependency* dep = &(current->dependencies[j]);
				int len = getDependencyString(dependencyString, dep->lock, dep->holdingSet, dep->holdingCount);
				if(len < 0) {
					fprintf(stderr, "Error");
					abort();
				}
				if(!_dependencyMap.find(dependencyString, len, &depGlobal)) {
					// this is a brand new dependency
#ifdef ENABLE_PREVENTION
					depGlobal = new Dependency(dep->lock, dep->realLock, dep->holdingSet, dep->holdingCount, dep->condRelated);
#else
					depGlobal = new Dependency(dep->lock, dep->holdingSet, dep->holdingCount);
#endif
					char* newDepStr = new char[len + 1];
					strcpy(newDepStr, dependencyString);
					_dependencyMap.insert(dependencyString, len, depGlobal);
					depCount++;
				} else {
					// we already have this
#ifdef ENABLE_PREVENTION
					depGlobal->condRelated = dep->condRelated;
#endif
				}
#ifdef ENABLE_LOG
				_logFile<<"    "<<dep->lock<<" : ";
				for(int i = 0; i < dep->holdingCount; i++) {
					_logFile<<dep->holdingSet[i]<<", ";
				}
				_logFile<<endl;
#endif
			}
		}
#ifdef ENABLE_LOG
		_logFile.close();
#endif
		//fprintf(stderr, "record amount of dep %zu unique dep %zu\n", depAmount, depCount);
		return depCount;
	}

	// build the dependency string
	int getDependencyString(char* str, void* lock, void** holdingSet, int len) {
		str[0] = '\0';
		int length = sprintf(str, "%lx", (uintptr_t)lock);
		for(int i = 0; i < len; i++) {
			length += sprintf(str + length, "%lx", (uintptr_t)holdingSet[i]);
		}
		str[length] = '\0';
		return length;
	}

	// detection based on iGoodlock
	void detect() {
		// initilize statistic data;
		_deadlockReported = _deadlockFound = 0;
		_deadlockMap.initialize(HashFuncs::hashString, HashFuncs::compareString, xdefines::MAX_DEADLOCK);
		int visiting;
		ChainStack* stack = new ChainStack;
		bool* isTraversed = new bool[_threadIndex];
		memset(isTraversed, 0, _threadIndex * sizeof(bool));
		for(int threadIndex = 0; threadIndex < _threadIndex - 1; threadIndex++) {
			real_thread_t* thread = &threadsInfoReal[threadIndex];
			if(thread->depCount == 0) continue;
			visiting = threadIndex;
			for(size_t j = 0; j < thread->depCount; j++) {
				Dependency* dep = &(thread->dependencies[j]);
				isTraversed[threadIndex] = true;
				stack->push(dep);
				dfs(stack, visiting, isTraversed);
				stack->pop();
			}
		}
		delete stack;
		delete[] isTraversed;
	}

	// check if adding dep to chain will still be a chain
	bool isChain(ChainStack *stack, Dependency* dep) {
		for(ChainList* cl = stack->list->next; cl != NULL; cl = cl->next) {
			if(cl->depEntry == dep) return false; 
			if(cl->depEntry->lock == dep->lock) return false;
			for(int i = 0; i < cl->depEntry->holdingCount; i++) {
				for(int j = 0; j < dep->holdingCount; j++) {
					if(cl->depEntry->holdingSet[i] == dep->holdingSet[j]) return false;
				}
			}
		}
		for(int j = 0; j < dep->holdingCount; j++) {
			if(stack->tail->depEntry->lock == dep->holdingSet[j]) return true;
		}
		return false;
	}

	// check if adding dep to chain will give a cycle chain
	bool isCycleChain(ChainStack *stack, Dependency* dep) {
		for(int j = 0; j < stack->list->next->depEntry->holdingCount; j++) {
			if(stack->list->next->depEntry->holdingSet[j] == dep->lock) return true;
		}
		return false;
	}

	// dfs to check chain
	void dfs(ChainStack* stack, int visiting, bool* isTraversed) {
		for(int threadIndex = visiting + 1; threadIndex < _threadIndex; threadIndex++) {
			real_thread_t* thread = &threadsInfoReal[threadIndex];
			if(thread->depCount == 0) continue;
			if(!isTraversed[threadIndex]) {
				for(size_t j = 0; j < thread->depCount; j++) {
					Dependency* dep = &(thread->dependencies[j]);
					if(isChain(stack, dep)) {
						if(isCycleChain(stack, dep)) {
							reportDeadlock(stack, dep);
						} else {
							isTraversed[threadIndex] = true;
							stack->push(dep);
							dfs(stack, visiting, isTraversed);
							stack->pop();
							isTraversed[threadIndex] = false;
						}
					}
				}
			}
		}
	}

	// output deadlocks detected from current status
	// current chain will be the whole cycle
	void reportDeadlockCurrent(ChainStack *stack) {
		fprintf(stderr, "Deadlock: \n");
		for(ChainList* cl = stack->list->next; cl != NULL; cl = cl->next) {
			fprintf(stderr, "%p -> ", cl->depEntry->lock);
		}
	}

	// output all deadlocks detected in the end
	// chain + dep will be the whole cycle
	void reportDeadlock(ChainStack *stack, Dependency* dep) {
		_deadlockReported++;
		vector<uintptr_t> locks; // record locks involved in this deadlock 
		size_t size = 0; // how many locks in this deadlock
		bool needMerge = true;
		fprintf(stderr, "Deadlock: \n");
#ifdef REPORTFILE
		_reportFile<<"Deadlock: "<<endl;
#endif
		for(ChainList* cl = stack->list->next; cl != NULL; cl = cl->next) {
			fprintf(stderr, "%p -> ", cl->depEntry->lock);
#ifdef REPORTFILE
			_reportFile<<cl->depEntry->lock<<" -> ";
#endif
#ifdef DETAILREPORT
			getLockCallSite(cl->depEntry);
#endif
			locks.push_back((uintptr_t)cl->depEntry->lock);
			size++;
#ifdef ENABLE_PREVENTION
			if(cl->depEntry->condRelated) needMerge = false;
#endif
		}
		fprintf(stderr, "%p ->", dep->lock);
#ifdef REPORTFILE
		_reportFile<<dep->lock<<" -> ";
#endif
#ifdef DETAILREPORT
		getLockCallSite(dep);
#endif
#ifdef REPORTFILE
		_reportFile<<endl;
#endif
		fprintf(stderr, "\n");
		locks.push_back((uintptr_t)dep->lock);
#ifdef ENABLE_PREVENTION
		if(dep->condRelated) needMerge = false;
#endif
		size++;
		// roughly check repeated deadlock sets 
		sort(locks.begin(), locks.end());
		char lockStr[MAXBUFSIZE];
		lockStr[0] = '\0';
		size_t len = 0;
		for(size_t i = 0; i < size; i++) {
			len += sprintf(lockStr + len, "%lx", locks[i]);
		}
		lockStr[len] = '\0';
		char ret;
		if(!_deadlockMap.find(lockStr, len, &ret)) {
			// this is a new deadlock
			char* newStr = new char[len + 1];
			strcpy(newStr, lockStr);
			_deadlockMap.insert(newStr, len, 0);
			_deadlockFound++;
#ifdef ENABLE_PREVENTION
			// if related to cond, don't apply merging on it
			if(!needMerge) return;
			char tmp;
			MergeSetList *temp = new MergeSetList();
			for(size_t i = 0; i < size; i++) temp->mergeSet.insert((void*)locks[i]);
			_mergeSetTail->next = temp;
			_mergeSetTail = _mergeSetTail->next;
#endif
		}
	}

#ifdef ENABLE_PREVENTION
	// union merge sets if there're intersections
	void mergeSetUnion() {
		int count = 0;
		for(MergeSetList* i = _mergeSetList->next; i != NULL; i = i->next) {
			fprintf(stderr, "merge set #%d:", count++);
			for(MergeSet::iterator iter = i->mergeSet.begin(); iter != i->mergeSet.end(); iter++) {
				fprintf(stderr, " %p", *iter);
			}
			fprintf(stderr, "\n");
		}
		MergeSet temp;
		bool flag = true;
		// keep union merge sets till there's no intersection
		while(flag) {
			flag = false;
			for(MergeSetList* i = _mergeSetList->next; i != NULL; i = i->next) {
				MergeSetList* jHead = i;
				for(MergeSetList* j = i->next; j != NULL; j = j->next) {
					temp.clear();
					set_intersection(i->mergeSet.begin(),i->mergeSet.end(),j->mergeSet.begin(),j->mergeSet.end(),insert_iterator<MergeSet>(temp,temp.begin()));
					if(!temp.empty()) {
						flag = true;
						set_union(i->mergeSet.begin(),i->mergeSet.end(),j->mergeSet.begin(),j->mergeSet.end(),insert_iterator<MergeSet>(i->mergeSet,i->mergeSet.begin()));
						jHead->next = j->next;
						continue;
					}
					jHead = j;
				}
			}
		}
		count = 0;
		for(MergeSetList* i = _mergeSetList->next; i != NULL; i = i->next) {
			fprintf(stderr, "after union, merge set #%d:", count++);
			for(MergeSet::iterator iter = i->mergeSet.begin(); iter != i->mergeSet.end(); iter++) {
				fprintf(stderr, " %p", *iter);
			}
			fprintf(stderr, "\n");
		}
	}

	// return the merge set that includes the addr
	MergeSetList* getRelatedMergeSet(void* addr) {
		for(MergeSetList* i = _mergeSetList->next; i != NULL; i = i->next) {
			if(i->mergeSet.find(addr) != i->mergeSet.end()) {
				return i;
			}
		}
		return NULL;
	}

	// write call stacks into deadlock history file
	void writeCallstack(void* lock) {
		void* realLock = NULL;
		if(!_realMutexMap.find(lock, sizeof(void*), &realLock)) {
			fprintf(stderr, "Cannot find real mutex for %p!", lock);
		}
		if(realLock != NULL) {
			my_mutex* myMutex = (my_mutex*)realLock;
			callstack* myStack;
			if((void*)myMutex != lock) {
				myStack = myMutex->callsite;
			} else {
				myStack = NULL;
			}
			while(myStack != NULL) {
				for(int i = 0; i < myStack->found; i++) {
					_deadlockFile<<" "<<(uintptr_t)myStack->stack[i];
				}
				myStack = myStack->next;
			}
		}
		_deadlockFile<<"."<<endl;
	}

	// do merge till no merge is required anymore, then write final merge set into file
	void generateMergeSetInfo() {
		if(_deadlockReported > 0) {
			_realMutexMap.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, xdefines::MAX_SYNC_ITEMS);
			for(DependencyHashMap::iterator iter = _dependencyMap.begin(); iter != _dependencyMap.end(); iter++) {
				Dependency* dep = iter.getData();
				_realMutexMap.insertIfAbsent(dep->lock, sizeof(void*), dep->realLock);
			}
			bool flag = true; // whether we need to check merge again
			while(flag) {
				flag = false; // if we do some merge, the flag will be true and then we will check it all over again
				// intuitive union
				mergeSetUnion();
				// current version is a conservative merging
				// check every dependency in dependencymap
				for(DependencyHashMap::iterator iter = _dependencyMap.begin(); iter != _dependencyMap.end(); iter++) {
					Dependency* dep = iter.getData();
					if(dep->condRelated) continue;
					MergeSetList* msl = getRelatedMergeSet(dep->lock);
					if(msl) {
						for(int i = 0; i < dep->holdingCount - 1; i++) {
							if(msl->mergeSet.find(dep->holdingSet[i]) != msl->mergeSet.end()) {
								// now this dependency is related to a mergeset
								for(int j = i + 1; j < dep->holdingCount; j++) {
									if(msl->mergeSet.find(dep->holdingSet[j]) == msl->mergeSet.end()) {
										// holding other locks between two deadlock-related locks
										msl->mergeSet.insert(dep->holdingSet[j]);
										flag = true;
									}
								}
								break;
							}
						}
					}
					if(flag) break;
				}
			}
			// wrintg into file
			fprintf(stderr, "Recording merge set infomation for deadlock prevention.\n");
			for(MergeSetList* i = _mergeSetList->next; i != NULL; i = i->next) {
				_deadlockFile<<"-"<<endl;
				//callstack* myStack;
				for(MergeSet::iterator iter = i->mergeSet.begin(); iter != i->mergeSet.end(); iter++) {
					if((INDIRECTION_MASK & (uintptr_t)*iter) == INDIRECTION_MASK) {
						// a special lock is needed to be merged
						int index = getSpecialLockIndex(*iter);
						special_info_list* head = prevention::getInstance().specialList;
						special_info_list* sll = head->next;
						while(index-- > 0) {
							if(sll == NULL) abort();
							sll = sll->next;
							head = head->next;
						}
						head->next = sll->next;
						for(auto si = sll->list->next; si != NULL; si = si->next) {
							_deadlockFile<<" "<<(uintptr_t)si->entry->lock;
							for(int i = 0; i < si->entry->callsite->found; i++) {
								_deadlockFile<<" "<<(uintptr_t)si->entry->callsite->stack[i];
							}
							_deadlockFile<<"."<<endl;
						}
					} else {
						_deadlockFile<<" "<<(uintptr_t)(*iter);
						writeCallstack(*iter);
					}
				}
			}
		}
		// recover previous history
		for(special_info_list* sll = prevention::getInstance().specialList->next; sll != NULL; sll = sll->next) {
			_deadlockFile<<"-"<<endl;
			for(auto si = sll->list->next; si != NULL; si = si->next) {
				_deadlockFile<<" "<<(uintptr_t)si->entry->lock;
				for(int i = 0; i < si->entry->callsite->found; i++) {
					_deadlockFile<<" "<<(uintptr_t)si->entry->callsite->stack[i];
				}
				_deadlockFile<<"."<<endl;
			}
		}
	}
#endif 

private:
	std::string exec(const char* cmd) {
		FILE* pipe = popen(cmd, "r");
		if (!pipe) return "ERROR";
		char buffer[128];
		std::string result = "";
		while (!feof(pipe)) {
			if (fgets(buffer, 128, pipe) != NULL)
				result += buffer;
		}
		pclose(pipe);
		return result;
	}

	string addrToLine(void* addr) {
		char buf[MAXBUFSIZE];
		buf[0] = '\0';
		sprintf(buf, "addr2line -a -i -e %s %p | tail -1", __progname_full, (void*)((uintptr_t)addr - PREV_INSTRUCTION_OFFSET));
		return exec(buf);
	}

	void getLockCallSite(Dependency* dep) {
		if(dep->callsiteCount == 0) return;
		for(int i = 0; i < dep->callsiteCount; i++) {
			fprintf(stderr, "\n  Callsites #%d:\n", i);
#ifdef REPORTFILE
			_reportFile<<"\n  Callsites #"<<i<<endl;
#endif
			string sourceLine = addrToLine(dep->callerAddr[i][0]);
			fprintf(stderr, "    %s", sourceLine.c_str());
#ifdef REPORTFILE
			_reportFile<<"    "<<sourceLine;
#endif
			if((uintptr_t)dep->callerAddr[i][1] != 0) {
				sourceLine = addrToLine(dep->callerAddr[i][1]);
				fprintf(stderr, "    %s", sourceLine.c_str());
#ifdef REPORTFILE
				_reportFile<<"    "<<sourceLine;
#endif
			}
		}
	}

private:
	typedef HashMap<char*, Dependency*, HeapAllocator> DependencyHashMap;
	DependencyHashMap _dependencyMap;
	int _threadIndex; // used index
#ifdef REPORTFILE
	ofstream _reportFile; // report file
#endif
#ifdef ENABLE_LOG
	ofstream _logFile; // depdendencies log file
#endif
	deadlock_info _deadlockInfo[xdefines::MAX_DEADLOCK];	// record a deadlock basic info. 
	int _deadlockFound;	// how many different deadlocks we found
	size_t _deadlockReported; // how many deadlocks we found
	typedef HashMap<char*, char, HeapAllocator> DeadlockHashMap;
	DeadlockHashMap _deadlockMap;
#ifdef ENABLE_PREVENTION
	typedef HashMap<void*, void*, HeapAllocator> RealMutexMap;
	RealMutexMap _realMutexMap;
	ofstream _deadlockFile; // deadlock history file
	MergeSetList *_mergeSetList;	// merge set list
	MergeSetList *_mergeSetTail;	// merge set end, aka the insert point
#endif
};
#endif
