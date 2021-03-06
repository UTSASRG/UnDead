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
* @file heapallocator.hh
* @brief heap allocator for hash map
* @author Tongping Liu, <http://www.cs.utsa.edu/~tongpingliu/>
* @author Jinpeng Zhou, Jinpeng.Zhou@utsa.edu
*/
#ifndef __HEAPALLOCATOR_HH__
#define __HEAPALLOCATOR_HH__

// For hashmap

class HeapAllocator {
public:
  static void* allocate(size_t sz) {
    void* ptr =  malloc(sz);
    if(!ptr) {
      return NULL;
    }
    return ptr;
  }

  static void deallocate(void* ptr) {
    free(ptr);
    ptr = NULL;
  }
};
#endif
