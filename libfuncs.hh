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
* @file libfuncs.hh
* @brief Intercept functions
* @author Tongping Liu, <http://www.cs.utsa.edu/~tongpingliu/>
* @author Jinpeng Zhou, Jinpeng.Zhou@utsa.edu
*/
#ifndef __LIBFUNCS_HH__
#define __LIBFUNCS_HH__

#define WRAP(x) _real_##x

extern int (*WRAP(pthread_create))(pthread_t*, const pthread_attr_t*, void *(*)(void*), void*);

extern int (*WRAP(pthread_join))(pthread_t, void**);

extern int (*WRAP(pthread_mutex_init))(pthread_mutex_t*, const pthread_mutexattr_t*);
extern int (*WRAP(pthread_mutex_destroy))(pthread_mutex_t*);
extern int (*WRAP(pthread_mutex_lock))(pthread_mutex_t*);
extern int (*WRAP(pthread_mutex_unlock))(pthread_mutex_t*);
extern int (*WRAP(pthread_mutex_trylock))(pthread_mutex_t*);

extern int (*WRAP(pthread_cond_timedwait))(pthread_cond_t*, pthread_mutex_t*, const struct timespec*);
extern int (*WRAP(pthread_cond_wait))(pthread_cond_t*, pthread_mutex_t*);

void init_real_functions();
#endif
