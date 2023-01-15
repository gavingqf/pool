// single object pool with listh.h
// Copyright (c) 2019 - 2023 gavingqf (gavingqf@126.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once
/*
 * common object pool and memory pool;
 */
#include <stdlib.h>
#include <assert.h>
#include "mutex_interface.h"
#include "list.h"
#include <memory>
#include <mutex>

namespace SObjPoolSpace {
	typedef struct list_head list_head;

	// alloc and free macro.
    #define ObjMalloc ::malloc
    #define ObjFree   ::free

	// function for alloc and free.
	static inline void* obj_alloc(size_t size) {
		return ObjMalloc(size);
	}
	static inline void obj_free(void *data) {
		ObjFree(data);
	}

	// construct is whether construct when get object.
	// where locker can use std::mutex or CNonMutex.
	template <typename T, 
		bool construct = true, 
		typename locker = CNonMutex
	>
	class object_pool final {
	public:
		typedef T value_type;

	private:
		// value wrap.
		struct value_info {
			// value must be in the front of value_info.
			value_type value;

			// reference num.
			int magic_num;

			// the double link node.
			list_head  link;
		};

		// resource info.
		struct resource_info {
			list_head  link;
			value_info *array;
			int        size;
		};

	public:
		explicit object_pool() : m_initSize(0), m_growSize(0) {
			INIT_LIST_HEAD(&m_freeLists);
			INIT_LIST_HEAD(&m_allLists);
		}
		virtual ~object_pool() {
			m_freeLocker.lock();
			INIT_LIST_HEAD(&m_freeLists);
			m_freeLocker.unlock();

			// release all resource.
			list_head *pos, *n;
			resource_info *pvalue;
			list_for_each_safe(pos, n, &m_allLists) {
				if ((pvalue = list_entry(pos, resource_info, link))) {
					obj_free(pvalue->array);
					obj_free(pvalue);
				} else {
					assert(false && "get value pointer error");
				}
			}
		}

	public:
		// init.
		bool init(int init_size, int grow_size) {
			m_initSize = init_size;
			m_growSize = grow_size;
			if (0 == m_initSize && 0 == m_growSize) {
				return false;
			}

			if (0 == m_initSize) {
				m_initSize = m_growSize;
			} else if (0 == m_growSize) {
				m_growSize = m_initSize;
			}
			return this->allocate(m_initSize);
		}

		// fetch object.
		// compatible with all construction.
		template <typename ...Args>
		value_type* fetch_obj(Args&& ...args) {
			value_type* obj = _fetch();
			if (!obj) return nullptr;
			new (obj) value_type(std::forward<Args>(args)...);
			return obj;
		}

		// release object.
		void release_obj(value_type* value) {
			if (!value) return;

			// transfer to wrap_info.
			value_info *shell = (value_info*)value;
			if (!shell) return;
			assert(shell && "shell is null");

			assert(shell->magic_num == m_reference && "reference info is error");
			if (shell->magic_num != m_reference) return;

			// pointer check
			list_head *pos, *n;
			resource_info *pvalue;
			list_for_each_safe(pos, n, &m_allLists) {
				if ((pvalue = list_entry(pos, resource_info, link))) {
					if (shell >= pvalue->array && shell < pvalue->array + pvalue->size) {
						goto succ; // find it is valid pointer, then goto succ to put back.
					}
				} else {
					assert(false && "get value pointer error");
					return;
				}
			}
			assert(false && "can not find user pointer");
			return;

		succ:
			// set unreferenced.
			shell->magic_num = m_unreference;

			// call destructor as possible.
			if (construct) {
				shell->value.~value_type();
			}

			m_freeLocker.lock();
			list_add_tail(&shell->link, &m_freeLists);
			m_freeLocker.unlock();
		}

	protected:
		// allocate memory.
		bool allocate(int size) {
			assert(size > 0 && "size <= 0");
			if (size <= 0) return false;

			// new resource and push back
			value_info* array = (value_info*)obj_alloc(sizeof(value_info) * size);
			if (!array) {
				return false;
			}

			// add the memory.
			for (int i = 0; i < size; i++) {
				list_add_tail(&array[i].link, &m_freeLists);
			}

			// push to all list objects
			resource_info *t = (resource_info *)obj_alloc(sizeof(resource_info));
			assert(t && "alloc resource info error");
			if (!t) {
				return false;
			}

			t->array = array;
			t->size  = size;
			list_add_tail(&t->link, &m_allLists);
			return true;
		}

		// fetch pointer.
		value_type* _fetch() {
			// lock it
			m_freeLocker.lock();
			if (list_empty(&m_freeLists)) { // realloc it.
				if (!this->allocate(m_growSize)) {
					m_freeLocker.unlock();
					return nullptr;
				}
			}

			// get first object.
			value_info *object = list_first_entry(&m_freeLists, value_info, link);
			assert(object && "object is null");
			list_del(&object->link);
			m_freeLocker.unlock();

			object->magic_num = m_reference;
			return &object->value;
		}

	protected:
		list_head     m_allLists;                  // all resources
		list_head     m_freeLists;                 // allocated resource list
		locker        m_freeLocker;                // allocated resource locker
		int           m_initSize;                  // initialize size
		int           m_growSize;                  // grow size
		static const int m_reference = 0xA110CAED; // reference num.
		static const int m_unreference= 0xDEA110CA;// unreferenced num.
	};

	//////////////////////////////////////////////////////////////////////
	//                    memory buckets                               //
	/////////////////////////////////////////////////////////////////////

	// memory pool struct.
	template <int N>
	struct sizeMemory {
		int size;
		char data[N];
		operator char*() {
			return data;
		}
	};

	// memory buckets class which is thread safe.
	class memoryBuckets final{
	public:
		memoryBuckets() {
			const int initSize = 32, growSize = 4;
			auto ret =
				m_memory4.init(initSize, growSize) &&
				m_memory8.init(initSize, growSize) &&
				m_memory16.init(initSize, growSize) &&
				m_memory24.init(initSize, growSize) &&
				m_memory32.init(initSize, growSize) &&
				m_memory48.init(initSize, growSize) &&
				m_memory64.init(initSize, growSize) &&
				m_memory80.init(initSize, growSize) &&
				m_memory96.init(initSize, growSize) &&
				m_memory112.init(initSize, growSize) &&
				m_memory128.init(initSize, growSize) &&
				m_memory144.init(initSize, growSize) &&
				m_memory160.init(initSize, growSize) &&
				m_memory196.init(initSize, growSize) &&
				m_memory212.init(initSize, growSize) &&
				m_memory228.init(initSize, growSize) &&
				m_memory256.init(initSize, growSize) &&
				m_memory512.init(initSize, growSize);
			assert(ret && "initialize memory pool error");
		}
		virtual ~memoryBuckets() {}
		memoryBuckets(const memoryBuckets&) = delete;
		memoryBuckets& operator=(const memoryBuckets&) = delete;

		// all kinds of memory.
		using sizeMemory4   = sizeMemory<4>;   // 4 
		using sizeMemory8   = sizeMemory<8>;   // 8
		using sizeMemory16  = sizeMemory<16>;  // 16
		using sizeMemory24  = sizeMemory<24>;  // 24
		using sizeMemory32  = sizeMemory<32>;  // 32
		using sizeMemory48  = sizeMemory<48>;  // 48
		using sizeMemory64  = sizeMemory<64>;  // 64
		using sizeMemory80  = sizeMemory<80>;  // 80
		using sizeMemory96  = sizeMemory<96>;  // 96
		using sizeMemory112 = sizeMemory<112>; // 112
		using sizeMemory128 = sizeMemory<128>; // 128
		using sizeMemory144 = sizeMemory<144>; // 144
		using sizeMemory160 = sizeMemory<160>; // 160
		using sizeMemory196 = sizeMemory<196>; // 196
		using sizeMemory212 = sizeMemory<212>; // 212
		using sizeMemory228 = sizeMemory<228>; // 228
		using sizeMemory256 = sizeMemory<256>; // 256
		using sizeMemory512 = sizeMemory<512>; // 512
		// you can add more sizeMemory struct here.

	public:
		// Alloc allocates size memory.
		char* Alloc(int size) {
			if (size <= 0) {
				return nullptr;
			}

			// create object macro.
        #define createObj(Size) { \
            auto obj = m_memory##Size.fetch_obj();\
			obj->size = Size; \
			return *obj;      \
        }

			const int alignSize = 4;
			size = (size + alignSize - 1) & (~(alignSize - 1));
			if (size <= 4) {
				createObj(4);
			} else if (size <= 8) {
				createObj(8);
			} else if (size <= 16) {
				createObj(16);
			} else if (size <= 32) {
				createObj(32);
			} else if (size <= 48) {
				createObj(48);
			} else if (size <= 64) {
				createObj(64);
			} else if (size <= 80) {
				createObj(80);
			} else if (size <= 96) {
				createObj(96);
			} else if (size <= 112) {
				createObj(112);
			} else if (size <= 128) {
				createObj(128);
			} else if (size <= 144) {
				createObj(144);
			} else if (size <= 160) {
				createObj(160);
			} else if (size <= 196) {
				createObj(196);
			} else if (size <= 212) {
				createObj(212);
			} else if (size <= 228) {
				createObj(228);
			} else if (size <= 256) {
				createObj(256);
			} else if (size <= 512) {
				createObj(512);
			} else {
				char *p = (char*)ObjMalloc(sizeof(int)+size);
				*(int*)p = size;
				return (p+sizeof(int));
			}
		}

		// Dealloc deallocates the p memory.
		void Dealloc(char *p) {
			if (p == nullptr) {
				return;
			}

			// release object macro.
        #define releaseObj(size,p) { \
            m_memory##size.release_obj(((sizeMemory##size*)(p))); \
		}

			// calculate the memory's size.
			p -= sizeof(int);
			auto size = *(int*)p;
			if (size < 0) {
				assert("invalid memory size");
				return;
			}

			if (size <= 4) {
				releaseObj(4, p);
			} else if (size <= 8) {
				releaseObj(8, p);
			} else if (size <= 16) {
				releaseObj(16, p);
			} else if (size <= 32) {
				releaseObj(32, p);
			} else if (size <= 48) {
				releaseObj(48, p);
			} else if (size <= 64) {
				releaseObj(64, p);
			} else if (size <= 80) {
				releaseObj(80, p);
			} else if (size <= 96) {
				releaseObj(96, p);
			} else if (size <= 112) {
				releaseObj(112, p);
			} else if (size <= 128) {
				releaseObj(128, p);
			} else if (size <= 144) {
				releaseObj(144, p);
			} else if (size <= 160) {
				releaseObj(160, p);
			} else if (size <= 196) {
				releaseObj(196, p);
			} else if (size <= 212) {
				releaseObj(212, p);
			} else if (size <= 228) {
				releaseObj(228, p);
			} else if (size <= 256) {
				releaseObj(256, p);
			} else if (size <= 512) {
				releaseObj(512, p);
			} else {
				ObjFree(p);
			}
		}

	private:
		// all kinds of size memory pools;
		object_pool<sizeMemory4,false,std::mutex> m_memory4;
		object_pool<sizeMemory8,false,std::mutex> m_memory8;
		object_pool<sizeMemory16,false,std::mutex> m_memory16;
		object_pool<sizeMemory24,false,std::mutex> m_memory24;
		object_pool<sizeMemory32,false,std::mutex> m_memory32;
		object_pool<sizeMemory48,false,std::mutex> m_memory48;
		object_pool<sizeMemory64,false,std::mutex> m_memory64;
		object_pool<sizeMemory80,false,std::mutex> m_memory80;
		object_pool<sizeMemory96,false,std::mutex> m_memory96;
		object_pool<sizeMemory112,false,std::mutex> m_memory112;
		object_pool<sizeMemory128,false,std::mutex> m_memory128;
		object_pool<sizeMemory144,false,std::mutex> m_memory144;
		object_pool<sizeMemory160,false,std::mutex> m_memory160;
		object_pool<sizeMemory196,false,std::mutex> m_memory196;
		object_pool<sizeMemory212,false,std::mutex> m_memory212;
		object_pool<sizeMemory228,false,std::mutex> m_memory228;
		object_pool<sizeMemory256,false,std::mutex> m_memory256;
		object_pool<sizeMemory512,false,std::mutex> m_memory512;
	};
}