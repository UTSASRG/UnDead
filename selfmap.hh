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
* @file selfmap.hh
* @brief Process /proc/self/map file
* @author Tongping Liu, <http://www.cs.utsa.edu/~tongpingliu/>
* @author Jinpeng Zhou, Jinpeng.Zhou@utsa.edu
*/
#ifndef __SELFMAP_HH__
#define __SELFMAP_HH__

#include <fstream>
#include <string>
#include "interval.hh"

using namespace std;

struct regioninfo {
  void* start;
  void* end;
};

/**
 * A single mapping parsed from the /proc/self/maps file
 */
class mapping {
  public:
    mapping() : _valid(false) {}

    mapping(uintptr_t base, uintptr_t limit, char* perms, size_t offset, std::string file)
      : _valid(true), _base(base), _limit(limit), _readable(perms[0] == 'r'),
      _writable(perms[1] == 'w'), _executable(perms[2] == 'x'), _copy_on_write(perms[3] == 'p'),
      _offset(offset), _file(file) {}

    bool valid() const { return _valid; }

    bool isText() const { return _readable && !_writable && _executable; }

    bool isStack() const { return _file == "[stack]"; }

    bool isGlobals(std::string mainfile) const {
      // global mappings are RW_P, and either the heap, or the mapping is backed
      // by a file (and all files have absolute paths)
      // the file is the current executable file, with [heap], or with lib*.so
      // Actually, the mainfile can be longer if it has some parameters.
      return (_readable && _writable && !_executable && _copy_on_write) &&
        (_file.size() > 0 && (_file == mainfile ||  _file == "[heap]" || _file.find(".so") != std::string::npos));
    }

    //maybe it is global area
    bool isGlobalsExt() const {
      return _readable && _writable && !_executable && _copy_on_write && _file.size() == 0;
    }

    uintptr_t getBase() const { return _base; }

    uintptr_t getLimit() const { return _limit; }

    const std::string& getFile() const { return _file; }

  private:
    bool _valid;
    uintptr_t _base;
    uintptr_t _limit;
    bool _readable;
    bool _writable;
    bool _executable;
    bool _copy_on_write;
    size_t _offset;
    std::string _file;
};

/// Read a mapping from a file input stream
static std::ifstream& operator>>(std::ifstream& f, mapping& m) {
  if(f.good() && !f.eof()) {
    uintptr_t base, limit;
    char perms[5];
    size_t offset;
    size_t dev_major, dev_minor;
    int inode;
    string path;

    // Skip over whitespace
    f >> std::skipws;

    // Read in "<base>-<limit> <perms> <offset> <dev_major>:<dev_minor> <inode>"
    f >> std::hex >> base;
    if(f.get() != '-')
      return f;
    f >> std::hex >> limit;

    if(f.get() != ' ')
      return f;
    f.get(perms, 5);

    f >> std::hex >> offset;
    f >> std::hex >> dev_major;
    if(f.get() != ':')
      return f;
    f >> std::hex >> dev_minor;
    f >> std::dec >> inode;

    // Skip over spaces and tabs
    while(f.peek() == ' ' || f.peek() == '\t') {
      f.ignore(1);
    }

    // Read out the mapped file's path
    getline(f, path);

    m = mapping(base, limit, perms, offset, path);
  }

  return f;
}

class selfmap {
  public:
    static selfmap& getInstance() {
      static char buf[sizeof(selfmap)];
      static selfmap* theOneTrueObject = new (buf) selfmap();
      return *theOneTrueObject;
    }

	uintptr_t getStackTop() { return _mainStackTop; }

	uintptr_t getTextTop() { return _textTop; }

	void getTop(void** st, void** tt) {
		*st = (void*)_mainStackTop;
		*tt = (void*)_textTop;
	}
	
  private:
    selfmap() {
      // Read the name of the main executable
      // char buffer[PATH_MAX];
      //Real::readlink("/proc/self/exe", buffer, PATH_MAX);
      //_main_exe = std::string(buffer);
      // Build the mappings data structure
      ifstream maps_file("/proc/self/maps");

      //while(maps_file.good() && !maps_file.eof()) {
      mapping m;
			bool gotMainExe = false;

      while(maps_file >> m) {
        // It is more clean that that of using readlink. 
        // readlink will have some additional bytes after the executable file 
        // if there are parameters.	
				if(!gotMainExe) {
					_main_exe = std::string(m.getFile());
					gotMainExe = true;
				}
        if(m.isStack()) {
        //if(m.valid()) {
          //fprintf(stderr, "Base %lx limit %lx, %s\n", m.getBase(), m.getLimit(), m.getFile().c_str()); 
					_mainStackTop = m.getLimit();
        }

				if(m.isText() && m.getFile() == _main_exe) {
					_textTop = m.getLimit();
				}
      }
    }
		uintptr_t _mainStackTop;
		uintptr_t _textTop;
		std::string _main_exe;
};

#endif
