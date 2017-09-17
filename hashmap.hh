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
* @file hashmap.hh
* @brief hash map
* @author Tongping Liu, <http://www.cs.utsa.edu/~tongpingliu/>
* @author Jinpeng Zhou, Jinpeng.Zhou@utsa.edu
*/
#ifndef __HASHMAP_HH__
#define __HASHMAP_HH__

/*
 * @file   hashtable.h
 * @brief  Management about hash table. Used for single thread
 */

#include <assert.h>

#include "dllist.hh"
#include "xdefines.hh"

template <class KeyType,                    // What is the key? A long or string
          class ValueType,                  // What is the value there?
          class SourceHeap> // Where to call malloc
class HashMap {

  struct HashEntry {
    dllist list;
    size_t count; // How many _entries in this list

    void initialize() {
      count = 0;
      listInit(&list);
    }

    void* getFirstEntry() { return (void*)list.next; }
  };

  struct Entry {
    dllist list;
    KeyType key;
    size_t keylen;
    ValueType value;

    void initialize(KeyType ikey = 0, int ikeylen = 0, ValueType ivalue = 0) {
      listInit(&list);
      key = ikey;
      keylen = ikeylen;
      value = ivalue;
    }

    void erase() { listRemoveNode(&list); }

    struct Entry* nextEntry() { return (struct Entry*)list.next; }

    ValueType getValue() { return value; }

    KeyType getKey() { return key; }
  };

  bool _initialized;
  struct HashEntry* _entries;
  size_t _buckets;     // How many buckets in total
  size_t _bucketsUsed; // How many buckets in total

  typedef bool (*keycmpFuncPtr)(const KeyType, const KeyType, size_t);
  typedef size_t (*hashFuncPtr)(const KeyType, size_t);
  keycmpFuncPtr _keycmp;
  hashFuncPtr _hashfunc;

public:
  HashMap() : _initialized(false) {
  }

  void initialize(hashFuncPtr hfunc, keycmpFuncPtr kcmp, const size_t size = 4096) {
    _entries = NULL;
    _bucketsUsed = 0;
    _buckets = size;

    if(hfunc == NULL || kcmp == NULL) {
      abort();
    }

    // Initialize those functions.
    _hashfunc = hfunc;
    _keycmp = kcmp;

    // Allocated predefined size.
    _entries = (struct HashEntry*)SourceHeap::allocate(size * sizeof(struct HashEntry));

    // Initialize all of these _entries.
    struct HashEntry* entry;
    for(size_t i = 0; i < size; i++) {
      entry = getHashEntry(i);
      entry->initialize();
    }
    _initialized = true;
  }

  inline struct HashEntry* getHashEntry(size_t index) {
    if(index < _buckets) {
      return &_entries[index];
    } else {
      return NULL;
    }
  }

  inline size_t hashIndex(const KeyType& key, size_t keylen) {
    size_t hkey = _hashfunc(key, keylen);
    return hkey % _buckets;
  }

  // Look up whether an entry is existing or not.
  // If existing, return true. *value should be carried specific value for this key.
  // Otherwise, return false.
  bool find(const KeyType& key, size_t keylen, ValueType* value) {
    assert(_initialized == true);
    size_t hindex = hashIndex(key, keylen);
    struct HashEntry* first = getHashEntry(hindex);
    struct Entry* entry = getEntry(first, key, keylen);
    bool isFound = false;

    if(entry) {
      *value = entry->value;
      isFound = true;
    }

    return isFound;
  }

  void insert(const KeyType& key, size_t keylen, ValueType value) {
    assert(_initialized == true);
    size_t hindex = hashIndex(key, keylen);
    struct HashEntry* first = getHashEntry(hindex);

    insertEntry(first, key, keylen, value);
  }

  // Insert a hash table entry if it is not existing.
  // If the entry is already existing, return true
  bool insertIfAbsent(const KeyType& key, size_t keylen, ValueType value) {
    assert(_initialized == true);
    size_t hindex = hashIndex(key, keylen);
    struct HashEntry* first = getHashEntry(hindex);
    struct Entry* entry;
    bool isFound = true;


    // Check all _entries with the same hindex.
    entry = getEntry(first, key, keylen);
    if(!entry) {
      isFound = false;
      insertEntry(first, key, keylen, value);
    }

    return isFound;
  }

  // Free an entry with specified
  bool erase(const KeyType& key, size_t keylen) {
    assert(_initialized == true);
    size_t hindex = hashIndex(key, keylen);
    struct HashEntry* first = getHashEntry(hindex);
    struct Entry* entry;
    bool isFound = false;

    entry = getEntry(first, key, keylen);

    if(entry) {
      isFound = true;

      // Check whether this entry is the first entry.
      // Remove this entry if existing.
      entry->erase();

      SourceHeap::deallocate(entry);
    }

    first->count--;

    return isFound;
  }

  // Clear all _entries
  void clear() {}

private:
  // Create a new Entry with specified key and value.
  struct Entry* createNewEntry(const KeyType& key, size_t keylen, ValueType value) {
    struct Entry* entry = (struct Entry*)SourceHeap::allocate(sizeof(struct Entry));

    // Initialize this new entry.
    entry->initialize(key, keylen, value);
    return entry;
  }

  void insertEntry(struct HashEntry* head, const KeyType& key, size_t keylen, ValueType value) {
    // Check whether the first entry is empty or not.
    // Create an entry
    struct Entry* entry = createNewEntry(key, keylen, value);
    listInsertTail(&entry->list, &head->list);
    head->count++;
  }

  // Search the entry in the corresponding list.
  struct Entry* getEntry(struct HashEntry* first, const KeyType& key, size_t keylen) {
    struct Entry* entry = (struct Entry*)first->getFirstEntry();
    struct Entry* result = NULL;

    // Check all _entries with the same hindex.
    int count = first->count;
    while(count > 0) {
      if(entry->keylen == keylen && _keycmp(entry->key, key, keylen)) {
        result = entry;
        break;
      }

      entry = entry->nextEntry();
      count--;
    }

    return result;
  }

public:
  class iterator {
    friend class HashMap<KeyType, ValueType, SourceHeap>;
    struct Entry* _entry; // Which entry in the current hash entry?
    size_t _pos;          // which bucket at the moment? [0, nbucket-1]
    HashMap* _hashmap;

  public:
    iterator(struct Entry* ientry = NULL, int ipos = 0, HashMap* imap = NULL) {
      _pos = ipos;
      _entry = ientry;
      _hashmap = imap;
    }

    ~iterator() {}

    iterator& operator++(int) // in postfix ++  /* parameter? */
    {
      struct HashEntry* hashentry = _hashmap->getHashEntry(_pos);

      // Check whether this entry is the last entry in current hash entry.
      if(!isListTail(&_entry->list, &hashentry->list)) {
        // If not, then we simply get next entry. No need to change pos.
        _entry = _entry->nextEntry();
      } else {
        // Since current list is empty, we must search next hash entry.
        _pos++;
        while((hashentry = _hashmap->getHashEntry(_pos)) != NULL) {
          if(hashentry->count != 0) {
            // Now we can return it.
            _entry = (struct Entry*)hashentry->getFirstEntry();
            return *this;
          }
          _pos++;
        }

        _entry = NULL;
      }

      return *this;
    }

    // iterator& operator -- ();
    // Iterpreted as a = b is treated as a.operator=(b)
    iterator& operator=(const iterator& that) {
      _entry = that._entry;
      _pos = that._pos;
      _hashmap = that._hashmap;
      return *this;
    }

    bool operator==(const iterator& that) const { return _entry == that._entry; }

    bool operator!=(const iterator& that) const { return _entry != that._entry; }

    ValueType getData() { return _entry->getValue(); }

    KeyType getkey() { return _entry->getKey(); }
  };

  // Acquire the first entry of the hash table
  iterator begin() {
    size_t pos = 0;
    struct HashEntry* head = NULL;
    struct Entry* entry;

    // Get the first non-null entry
    while(pos < _buckets) {
      head = getHashEntry(pos);
      if(head->count != 0) {
        // Now we can return it.
        entry = (struct Entry*)head->getFirstEntry();
        return iterator(entry, pos, this);
      }
      pos++;
    }

    return end();
  }

  iterator end() { return iterator(NULL, 0, this); }
};

#endif
