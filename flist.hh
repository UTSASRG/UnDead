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
* @file flist.hh 
* @brief single-link list 
* @author Tongping Liu, <http://www.cs.utsa.edu/~tongpingliu/> 
* @author Jinpeng Zhou, Jinpeng.Zhou@utsa.edu
*/
#ifndef __FLIST_HH__
#define __FLIST_HH__

// single link list with head and tail
// each node contains an entry pointer
template<class Entry>
class EntryList {
public:
	class EntryLink {
		public:
			EntryLink(Entry* e = NULL) {
				entry = e;
				next = NULL;
			}
			Entry* entry;
			EntryLink* next;
	};

	EntryList() {
		tail = list = new EntryLink;
	}
	
	EntryLink* list;
	EntryLink* tail;

	bool isEmpty() { return list == tail; };

	/*
	EntryLink* locateEntry(Entry* e) {
		for(EntryLink* el = list->next; el != NULL; el = el->next) {
			if(*(el->entry) == *e) return el;
		}
		return NULL;
	}
	*/

	void insertToTail(Entry* e) {
		tail->next = new EntryLink(e);
		tail = tail->next;
	}
};
#endif

