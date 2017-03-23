#if !defined(_H_COROUTINE) 
#define _H_COROUTINE

#include "scoroutine.h"

#if defined(WIN32)
#include <Windows.h>
#else 
#include <ucontext.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

// 默认协程栈大小 和 初始化协程数量
#define _INT_STACK		(256 * 1024)
#define _INT_COROUTINE	(16)

// 声明协程结构 和 协程管理器结构
struct sco {
#if defined(WIN32)
	PVOID ctx;				// 当前协程运行的环境
#else 
	char * stack;			// 当前协程栈指针
	ucontext_t ctx;			// 当前协程运行的上下文环境
	ptrdiff_t cap;			// 当前栈的容量
	ptrdiff_t cnt;			// 当前栈的大小
#endif
	sco_f func;				// 协程体执行
	void * arg;				// 用户输入的参数
	int status;				// 当前协程运行状态 _SCO_*
};

struct scomng {
#if defined(WIN32)
	PVOID main;				// 当前主协程记录运行环境
#else 
	char stack[_INT_STACK];	// 当前协程中开辟的栈对象
	ucontext_t main;		// 当前协程上下文对象
#endif
	int running;			// 当前协程中运行的协程id

	struct sco ** cos;		// 协程对象集, 循环队列
	int cap;				// 协程对象集容量
	int idx;				// 当前协程集中轮询到的索引
	int cnt;				// 当前存在的协程个数
};

/*
 * 开启协程系统函数, 并返回创建的协程管理器
 *			: 返回创建的协程对象
 */
void * sco_open(void) {
	struct scomng * comng = (struct scomng *)malloc(sizeof(struct scomng));
	assert(NULL != comng);
	comng->running = -1;
	comng->cos = (struct sco **)calloc(_INT_COROUTINE, sizeof(struct sco *));
	comng->cap = _INT_COROUTINE;
	comng->idx = 0;
	comng->cnt = 0;
	assert(NULL != comng->cos);
#if defined(WIN32)
	// 在当前线程环境中开启Window协程
	comng->main = ConvertThreadToFiberEx(NULL, FIBER_FLAG_FLOAT_SWITCH);
#endif
	return comng;
}

// 销毁一个协程对象
static void _sco_delete(struct sco * co) {
#if defined(WIN32)
	DeleteFiber(co->ctx);
#else
	free(co->stack);
#endif
	free(co);
}

/*
 * 关闭已经开启的协程系统函数
 *	sco		: sco_oepn 返回的当前协程中协程管理器
 */
void sco_close(void * sco) {
	int i = -1;
	struct scomng * comng = (struct scomng *)sco;
	while (++i < comng->cap) {
		struct sco * co = comng->cos[i];
		if (co) {
			_sco_delete(co);
			comng->cos[i] = NULL;
		}
	}

	free(comng->cos);
	comng->cos = NULL;
	free(comng);
#if defined(WIN32)
	// 切换当前协程系统变回默认的主线程, 关闭协程系统
	ConvertFiberToThread();
#endif
}

// 构建 struct sco 协程对象
static struct sco * _sco_new(sco_f func, void * arg) {
	struct sco * co = (struct sco *)malloc(sizeof(struct sco));
	assert(co && func);
	co->func = func;
	co->arg = arg;
	co->status = _SCO_READY;
#if !defined(WIN32)
	co->stack = NULL;
	co->cap = 0;
	co->cnt = 0;
#endif
	return co;
}

/*
 * 创建一个协程, 此刻是就绪态
 *  sco		: 协程管理器
 *	func	: 协程体执行的函数体
 *  arg		: 协程体中传入的参数
 *			: 返回创建好的协程id
 */
int sco_create(void * sco, sco_f func, void * arg) {
	struct sco * co = _sco_new(func, arg);
	struct scomng * comng = (struct scomng *)sco;
	struct sco ** cos = comng->cos;
	int cap = comng->cap;
	// 下面开始寻找, 如果数据足够的话
	if (comng->cnt < comng->cap) {
		// 当循环队列去查找
		int idx = comng->idx;
		do {
			if (NULL == cos[idx]) {
				cos[idx] = co;
				++comng->cnt;
				++comng->idx;
				return idx;
			}
			idx = (idx + 1) % cap;
		} while (idx != comng->idx);

		assert(idx == comng->idx);
		return -1;
	}

	// 这里需要重新构建空间
	cos = (struct sco **)realloc(cos, sizeof(struct sco *) * cap * 2);
	assert(NULL != cos);
	memset(cos + cap, 0, sizeof(struct sco *) * cap);
	comng->cos = cos;
	comng->cap = cap << 1;
	++comng->cnt;
	cos[comng->idx] = co;
	return comng->idx++;
}
#if defined(WIN32)
static VOID WINAPI _sco_main(LPVOID ptr) {
	struct scomng * comng = (struct scomng *)ptr;
	int id = comng->running;
	struct sco * co = comng->cos[id];
	// 执行协程体
	co->func(comng, co->arg);
	co = comng->cos[id];
	co->status = _SCO_DEAD;
	// 跳转到主纤程体中销毁
	SwitchToFiber(comng->main);
}
#else
// 协程运行的主体
static inline void _sco_main(uint32_t low32, uint32_t hig32) {
	uintptr_t ptr = (uintptr_t)low32 | ((uintptr_t)hig32 << 32);
	struct scomng * comng = (struct scomng *)ptr;
	int id = comng->running;
	struct sco * co = comng->cos[id];
	// 执行协程体
	co->func(comng, co->arg);
	co = comng->cos[id];
	co->status = _SCO_DEAD;
	_sco_delete(co);
	comng->cos[id] = NULL;
	--comng->cnt;
	comng->idx = id;
	comng->running = -1;
}
#endif
/*
 * 通过协程id激活协程
 *	sco		: 协程系统管理器
 *	id		: 具体协程id, sco_create 返回的协程id
 */
void sco_resume(void * sco, int id) {
#if defined(WIN32)
	struct sco * co;
	struct scomng * comng = (struct scomng *)sco;
	int running;

	assert(comng && id >= 0 && id < comng->cap);

	// _SCO_DEAD 状态协程, 完全销毁其它协程操作
	running = comng->running;
	if (running != -1) {
		co = comng->cos[running];
		assert(co && co->status == _SCO_DEAD);
		comng->cos[running] = NULL;
		--comng->cnt;
		comng->idx = running;
		comng->running = -1;
		_sco_delete(co);
		if (running == id)
			return;
	}

	// 下面是协程 _SCO_READY 和 _SCO_SUSPEND 处理
	co = comng->cos[id];
	if ((!co) || (co->status != _SCO_READY && co->status != _SCO_SUSPEND))
		return;

	// Window特性创建纤程, 并保存当前上下文环境, 切换到创建的纤程环境中
	if (co->status == _SCO_READY)
		co->ctx = CreateFiberEx(_INT_STACK, 0, FIBER_FLAG_FLOAT_SWITCH, _sco_main, comng);

	co->status = _SCO_RUNNING;
	comng->running = id;
	comng->main = GetCurrentFiber();
	// 正常逻辑切换到创建的子纤程中
	SwitchToFiber(co->ctx);
#else
	uintptr_t ptr;
	struct sco * co;
	struct scomng * comng = sco;
	int status;
	int running = comng->running;
	assert(running == -1 && id >= 0 && id < comng->cap);

	// 下面是协程 _SCO_READY 和 _SCO_SUSPEND 处理
	co = comng->cos[id];
	if ((!co) || (status = co->status) == _SCO_DEAD)
		return;

	comng->running = id;
	co->status = _SCO_RUNNING;
	switch (status) {
	case _SCO_READY:
		// 兼容x64指针通过makecontext传入
		ptr = (uintptr_t)comng;
		// 构建栈和运行链
		getcontext(&co->ctx);
		co->ctx.uc_stack.ss_sp = comng->stack;
		co->ctx.uc_stack.ss_size = _INT_STACK;
		co->ctx.uc_link = &comng->main;
		makecontext(&co->ctx, (void(*)())_sco_main, 2, (uint32_t)ptr, (uint32_t)(ptr >> 32));
		// 保存当前运行状态到comng->main, 然后跳转到 co->ctx运行环境中
		swapcontext(&comng->main, &co->ctx);
		break;
	case _SCO_SUSPEND:
		// stack add is high -> low
		memcpy(comng->stack + _INT_STACK - co->cnt, co->stack, co->cnt);
		swapcontext(&comng->main, &co->ctx);
		break;
	default:
		assert(co->status && 0);
	}
#endif
}

#if defined(WIN32)
/*
 * 关闭当前正在运行的协程, 让协程处理暂停状态
 *	sco		: 协程系统管理器
 */
void sco_yield(void * sco) {
	struct sco * co;
	struct scomng * comng = (struct scomng *)sco;
	int id = comng->running;
	if ((id < 0 || id >= comng->cap) || !(co = comng->cos[id]))
		return;
	co->status = _SCO_SUSPEND;
	comng->running = -1;
	co->ctx = GetCurrentFiber();
	SwitchToFiber(comng->main);
}
#else
// 保存当前运行的堆栈信息
static void _sco_savestack(struct sco * co, char * top) {
	char dummy = 0;
	ptrdiff_t size = top - &dummy;
	assert(size <= _INT_STACK);
	if (co->cap < size) {
		free(co->stack);
		co->cap = size;
		co->stack = malloc(size);
	}
	co->cnt = size;
	memcpy(co->stack, &dummy, size);
}
/*
 * 关闭当前正在运行的协程, 让协程处理暂停状态
 *	sco		: 协程系统管理器
 */
void sco_yield(void * sco) {
	struct sco * co;
	struct scomng * comng = sco;
	int id = comng->running;
	if ((id < 0 || id >= comng->cap) || !(co = comng->cos[id]))
		return;
	assert((char *)&co > comng->stack);
	_sco_savestack(co, comng->stack + _INT_STACK);
	co->status = _SCO_SUSPEND;
	comng->running = -1;
	swapcontext(&co->ctx, &comng->main);
}

#endif
/*
 * 得到当前协程状态
 *	sco		: 协程系统管理器
 *	id		: 协程id
 *			: 返回 _SCO_* 相关的协程状态信息
 */
int sco_status(void * sco, int id) {
	struct scomng * comng = (struct scomng *)sco;
	assert(comng && id >= 0 && id < comng->cap);
	return comng->cos[id] ? comng->cos[id]->status : _SCO_DEAD;
}

/*
 * 当前协程系统中运行的协程id
 *	sco		: 协程系统管理器
 *			: 返回 < 0 表示没有协程在运行
 */
int sco_running(void * sco) {
	return ((struct scomng *)sco)->running;
}

#endif // !_H_SCOROUTINE$WINDS