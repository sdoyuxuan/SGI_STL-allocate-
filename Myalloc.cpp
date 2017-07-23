#ifndef _ALLOC_HPP
#define _ALLOC_HPP
#pragma once
#include<iostream>
#include<mutex>
using namespace std;
#define _THROW_BAD_ALLOC do{cerr<<"out of memeory"<<endl;  \
	exit(1); }while (false)// 其实这里如果windows 有类型Linux 的 pthread_exit的话，调pthread_exit才比较符合条件，因为只是子线程导致的异常没有必要使整个进程退出

class Malloc_alloc
{
private:
	// out of memory 处理
	static void* oom_malloc(size_t);
	static void* oom_realloc(void*, size_t);
	static void(*handerforoom)();
public:
	typedef void(*pFun)();
	static void* allocate(size_t n)
	{
		void * ret = malloc(n);
		if (ret == NULL)
		{
			ret = oom_malloc(n);
		}
		return ret;
	}
	static void* reallocate(void* p, size_t new_sz)
	{
		void * ret = realloc(p, new_sz); // new_sz ==0 时 ，realloc返回NULL 使用效果相当于free
		if (new_sz != 0 && ret == NULL)
		{
			ret = oom_realloc(p, new_sz);
		}
		return ret;
	}
	static pFun set_malloc_handler(pFun p)
	{
		pFun old;
		old = handerforoom;
		handerforoom = p;
		return old; //保存老的返回回去，为了当使用这个的用户使用完成时，恢复默认的设置，以待下次使用
	}
	static void deallocate(void*p)
	{
		free(p);
	}
};
void(*Malloc_alloc::handerforoom)() = NULL;//类内静态成员初始化
void * Malloc_alloc::oom_malloc(size_t size)
{
	void * ret = NULL;
	pFun my_oom_handler = Malloc_alloc::handerforoom; // 这里我创建一个局部的函数指针变量而不去用malloc_alloc 共有的静态的函数指针，是为了保住该函数是线程安全函数。
	for (;;)                                         // 因为每一个线程的栈空间是私有的。如果我直接使用共有的静态函数指针的话，可能我前一步判断它不为空，在正要使用时，一个线程就把它置为NULL，所以一当使用时就会立马崩溃
	{
		if (my_oom_handler == NULL) _THROW_BAD_ALLOC;
		(*my_oom_handler)();
		ret = malloc(size);
		if (ret != NULL) return ret;
	}
}
void * Malloc_alloc::oom_realloc(void * p, size_t newsize)
{
	pFun my_oom_handler = Malloc_alloc::handerforoom;
	for (;;)
	{
		if (my_oom_handler == NULL) _THROW_BAD_ALLOC;
		(*my_oom_handler)();
		p = realloc(p, newsize);
		if (p != NULL) return p;
	}
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*一级内存配置器完毕！！！！*/
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
enum { _ALIGN = 8 };
enum { MaxBytes = 128 };
enum { freeList_num = MaxBytes / _ALIGN };
template<bool threads>
class Defaultalloctemplate
{
public:
	union  block{
		block* free_list_next;
		char usr_data[1];
	};
private:
	static block* volatile free_list[freeList_num];
	static char * start_pool_free;
	static char * end_pool_free;
	static size_t heapsize;//这个代表 内存池总共向系统总申请了多少内存的大小
private:
	static size_t Round_up(size_t bytes) //向上对齐为八的倍数，具体操作就是给bytes的低三位对应的每一次都加上1，然后产生进位，保住bytes是向上调整了，然后再用按位与把低三位上的数全消为0
	{                                    //则剩下的数必为8的倍数 。填充的时候需要调
		return (bytes + _ALIGN - 1)&~(_ALIGN - 1);
	}
	static size_t freeList_index(size_t bytes) // 找相应小块内存在自由链表中的下标
	{
		return (bytes + _ALIGN - 1) / _ALIGN - 1;
	}
	static void* refill(size_t n);
public:
	static char* chunk_alloc(size_t size, int&nobjs);
private:
	static std::mutex mtx; // alloc 生命周期结束自动释放所以不存在内存泄漏 

	class _mutex{
	public:
		_mutex()
		{
			if (threads)
			{
				mtx.lock();
			}
		}
		~_mutex()
		{
			if (threads) mtx.unlock();
		}
	};
public:
	static void* Allocate(size_t);

	static void deallocate(void*p, size_t n);

	static void* reallocate(void*p, size_t old_sz, size_t new_sz);
};
template<bool threads>
std::mutex Defaultalloctemplate<threads>::mtx; // mtx变量默认构造函数
template<bool threads>
typename Defaultalloctemplate<threads>::block * volatile Defaultalloctemplate<threads>::free_list[freeList_num] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
template<bool threads>
char * Defaultalloctemplate<threads>::start_pool_free = NULL;
template<bool threads>
char * Defaultalloctemplate<threads>::end_pool_free = NULL;
template<bool threads>
size_t Defaultalloctemplate<threads>::heapsize = 0;
template<bool threads>
void * Defaultalloctemplate<threads>::Allocate(size_t n)
{
	void * ret = NULL;
	if (n > MaxBytes)
	{
		ret = Malloc_alloc::allocate(n);
		return ret;
	}
	if (threads)
	{
		_mutex _lock;
	}
	block *volatile * Index_List = free_list + freeList_index(n); // 代表该2级指针所指的一级指针变量内容的储存空间易变
	if (*Index_List == NULL)
	{
		ret = refill(Round_up(n));
	}
	else
	{
		ret = *Index_List;
		*Index_List = (*Index_List)->free_list_next;
	}
	return ret;
}
template<bool threads>
void  Defaultalloctemplate<threads>::deallocate(void*p, size_t n)
{
	if (n > 128)
	{
		Malloc_alloc::deallocate(p);
		return;
	}
	if (threads)
	{
		_mutex _lock;
	}
	block * volatile * Index_List = free_list + freeList_index(n);
	if (*Index_List == NULL)
	{
		*Index_List = (block*)P;
	}
	else
	{
		(block*)p->free_list_next = *Index_List;
		*Index_List = (block*)p;
	}
	return;
}
template<bool threads>
void* Defaultalloctemplate<threads>::reallocate(void*p, size_t new_sz,size_t old_sz)
{
	void * ret = NULL;
	if (old_sz>(size_t)MaxBytes&&new_sz > (size_t)MaxBytes)
	{
		return ret = Malloc_alloc::reallocate(p, new_sz);
	}
	void*ret = Allocate(new_sz);
	size_t copy_sz = (new_sz > old_sz) ? old_sz : new_sz; // 这里选最小的是因为，如果是用户是想扩大的话。//那么用来拷贝的大小肯定是之前老的的大小，如果用户是要缩小的话
	memcpy(ret, p, copy_sz);                               //那么拷贝的大小肯定是剪裁后的内存大小
	deallocate(p, old_sz);
	return ret;
}     
template<bool threads>
void * Defaultalloctemplate<threads>::refill(size_t n)
{
	int nobjs = 20; 
	void * chunk = (void*)chunk_alloc(n, nobjs);
	if (nobjs == 1)
	{
		return chunk;
	}
	void * Cur_obj = chunk;
	void * Next_obj = chunk + n;
	void * ret = Cur_obj;
	block * volatile* freelist_Index = free_list + freeList_index(n); // 这里没加锁是因为只有调用allocate才能进来，而allocate加了锁
	*freelist_Index = Next_obj;
	for (size_t idx = 1; idx < nobjs - 1; idx++) //这里当Cur_obj走到最后一块内存的前一块时要出来，因为最后一块内存的next要指向空
	{
		Cur_obj = Next_obj;
		Next_obj = (block*)((char*)Cur_obj + n);
		(block*)Cur_obj->free_list_next = (block*)Next_obj;
	}
	Cur_obj = Next_obj;
	(block*)Cur_obj->free_list_next = NULL;
	return ret;
}
template<bool threads>
char* Defaultalloctemplate<threads>::chunk_alloc(size_t size,int & nobjs) //分工明确 这个函数只负责从内存池申请函数，具体挂到链表上由refill完成
{
	char * ret;
	size_t total = size*nobjs;
	size_t rest= end_pool_free - start_pool_free;
	if (rest > total)
	{
		ret = start_pool_free;
		start_pool_free += rest;
		return ret;
	}
	if (size < rest)
	{
		ret = start_pool_free;
		nobjs = rest / size;
		total = size*nobjs;
		start_pool_free += total;
		return ret;
	}
	size_t get_bytes = 2 * total + Round_up((heapsize >> 4));
	if (rest>0)
	{
		((block*)start_pool_free)->free_list_next = free_list[freeList_index(rest)];//先把内存池剩余的那点内存先放到链表内，此时内存池为空在从空闲链表内释放一大块内存出来
		free_list[freeList_index(rest)] = (block*)start_pool_free;
	}
	start_pool_free = (char*)malloc(get_bytes); // flag B
	if (start_pool_free == NULL)
	{
		size_t nums = MaxBytes / _ALIGN;
		size_t idx = freeList_index(size);
		block* volatile * free_list_index = free_list;
		for (; idx < nums; idx++)  // 只从链表中取走了一块可匹配size大小的内存
		{
			if (free_list_index[idx] == NULL) continue;
			start_pool_free = (char*)free_list_index[idx];
			end_pool_free = start_pool_free + ((idx+1)*_ALIGN);
			return chunk_alloc(size, nobjs);
		}
		end_pool_free = NULL;//flag A //这里先至为NULL，为了防止下面调一级空配的allocate抛出异常，如果该异常只是导致相应线程退出，那么其他线程还在跑,只是end_pool_free还指向之前已连入链表的那一小块内存肯定就有问题了....
		start_pool_free = Malloc_alloc::allocate(get_bytes);//如果这里导致异常，相应线程在这里就挂掉了，其他线程正常运行...
	}
	heapsize = heapsize+get_bytes;// 这里更新heapsize... 
	end_pool_free = start_pool_free + get_bytes; // 这俩句代码是标签A 标签B公用的
	return chunk_alloc(size, nobjs);
}
#endif
