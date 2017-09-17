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
* @file hashfuncs.hh 
* @brief hash functions 
* @author Tongping Liu, <http://www.cs.utsa.edu/~tongpingliu/> 
* @author Jinpeng Zhou, Jinpeng.Zhou@utsa.edu
*/
#ifndef __HASHFUNCS_H__
#define __HASHFUNCS_H__

#include <string.h>

#define HASH_BSD_LEN (sizeof(void*) / sizeof(short))
#define MALLOC_PAGESHIFT 12
class HashFuncs {
public:
  // The following functions are borrowed from the STL.
  static size_t hashString(char* start, size_t len) {
    unsigned long __h = 0;
    char* __s = start;
		int i = 0;

    for(; i <= (int) len; i++, ++__s)
      __h = 5 * __h + *__s;

    return size_t(__h);
  }

  static size_t hashInt(const int x, size_t) { return x; }

  static size_t hashLong(long x, size_t) { return x; }

  static size_t hashUnsignedlong(unsigned long x, size_t) { return x; }

	static size_t hashAddr(void* p, size_t) {
		size_t sum;
		union {
			uintptr_t p;
			unsigned short a[HASH_BSD_LEN];
		} u;

		u.p = (uintptr_t)p >> MALLOC_PAGESHIFT; 
		sum = u.a[0];
		sum = (sum << 7) - sum + u.a[1];
		sum = (sum << 7) - sum + u.a[2];
		sum = (sum << 7) - sum + u.a[3];

		return sum;
	}

	static size_t hashAddrs(uintptr_t* addr, size_t len) {
		size_t result = ~0;
		for(size_t i = 0; i < len; i++) result ^= addr[i];
		return result;
	}

  static bool compareAddr(void* addr1, void* addr2, size_t) { return addr1 == addr2; }

  static bool compareInt(int var1, int var2, size_t) { return var1 == var2; }
  
	static bool compareUnsignedlong(unsigned long var1, unsigned long var2, size_t) { return var1 == var2; }

  static bool compareString(char* str1, char* str2, size_t len) {
    return strncmp(str1, str2, len) == 0;
  }
};

#endif
