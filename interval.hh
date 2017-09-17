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
* @file interval.hh
* @brief represents a continuous range of integers
* @author Tongping Liu, <http://www.cs.utsa.edu/~tongpingliu/>
* @author Jinpeng Zhou, Jinpeng.Zhou@utsa.edu
*/
#ifndef __INTERVAL_HH__
#define __INTERVAL_HH__

class interval {
public:
  /// Standard constructor
  interval(uintptr_t base, uintptr_t limit) : _base(base), _limit(limit) {}
  interval(void* base, void* limit) : interval((uintptr_t)base, (uintptr_t)limit) {}

  /// Unit interval constructor
  interval(uintptr_t p) : _base(p), _limit(p + 1) {}
  interval(void* p) : interval((uintptr_t)p) {}

  /// Default constructor for use in maps
  interval() : interval(nullptr, nullptr) {}

  /// Shift
  interval operator+(uintptr_t x) const { return interval(_base + x, _limit + x); }

  /// Shift in place
  void operator+=(uintptr_t x) {
    _base += x;
    _limit += x;
  }

  /// Comparison function that treats overlapping intervals as equal
  bool operator<(const interval& b) const { return _limit <= b._base; }
  
  bool operator==(const interval& b) const { return _limit >= b._limit && b._base >= _base; }

  /// Check if an interval contains a point
  bool contains(uintptr_t x) const { return _base <= x && x < _limit; }

  uintptr_t get_base() const { return _base; }
  uintptr_t get_limit() const { return _limit; }

private:
  uintptr_t _base;
  uintptr_t _limit;
};

#endif
