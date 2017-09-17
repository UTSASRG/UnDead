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
* @file mm.hh
* @brief Cutomized memory mapping
* @author Tongping Liu, <http://www.cs.utsa.edu/~tongpingliu/>
* @author Jinpeng Zhou, Jinpeng.Zhou@utsa.edu
*/
#ifndef __MM_HH__
#define __MM_HH__

#include <stddef.h>
#include <sys/mman.h>
#include <stdio.h>

class MM {
public:
#define ALIGN_TO_CACHELINE(size) (size % 64 == 0 ? size : (size + 64) / 64 * 64)

  static void mmapDeallocate(void* ptr, size_t sz) { munmap(ptr, sz); }

  static void* mmapAllocateShared(size_t sz, bool hugePage = false, int fd = -1, void* startaddr = NULL) {
    return allocate(true, sz, hugePage, fd, startaddr);
  }

  static void* mmapAllocatePrivate(size_t sz, bool hugePage = false, void* startaddr = NULL, int fd = -1) {
    return allocate(false, sz, hugePage, fd, startaddr);
  }

private:
  static void* allocate(bool isShared, size_t sz, bool hugePage, int fd, void* startaddr) {
    int protInfo = PROT_READ | PROT_WRITE;
    int sharedInfo = isShared ? MAP_SHARED : MAP_PRIVATE;
    sharedInfo |= ((fd == -1) ? MAP_ANONYMOUS : 0);
    sharedInfo |= ((startaddr != (void*)0) ? MAP_FIXED : 0);
    sharedInfo |= MAP_NORESERVE;

    void* ptr = mmap(startaddr, sz, protInfo, sharedInfo, fd, 0);
    if(ptr == MAP_FAILED) {
      fprintf(stderr, "Couldn't do mmap (%s) : startaddr %p, sz %lx, protInfo=%d, sharedInfo=%d\n",
            strerror(errno), startaddr, sz, protInfo, sharedInfo);
			exit(-1);
    } else if(!hugePage){
			if(madvise(ptr, sz, MADV_NOHUGEPAGE) != 0) {
				fprintf(stderr, "set NO HUGE PAGE failed! Use huge page instead.\n");
			}
    }

    return ptr;
  }
};

class InternalHeapAllocator {
public:
  static void* malloc(size_t sz);
  static void free(void* ptr);
  static void* allocate(size_t sz);
  static void deallocate(void* ptr);
};
#endif
