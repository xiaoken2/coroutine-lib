#ifndef _COROUTINE_H_
#define _COROUTINE_H_

#include <iostream>
#include <memory>
#include <atomic>
#include <functional>
#include <cassert>
#include <ucontext.h>
#include <unistd.h>

#include "scheduler.h"

class Scheduler;

    // 公有继承：继承这个类的对象可以安全地生成一个指向自身的std::shared_ptr
    // 通过shared_from_this方法获取指向调用对象的shared_ptr,对比this指针
class Fiber : public std::enable_shared_from_this<Fiber> {
public:
    // 协程的三种状态
    enum State {
        READY,
        RUNNING,
        TERM
    };

private:
    Fiber(); // 默认构造函数，用于创建线程的第一个协程，即这个中线程main函数对应的协程，也是主协程

public:
    // 用于创建子协程的构造函数

    Fiber(std::function<void()> cb, size_t stacksize = 0, bool run_in_scheduler = true);

    ~Fiber();

    void reset(std::function<void()> cb);

    void resume();

    void yield();

    uint64_t getId() const {return m_id;}

    State getState() const {return m_state;}

    void setState(State st) {m_state = st;}

public:
    static void SetThis(Fiber *f);
    static std::shared_ptr<Fiber> GetThis();
    static uint64_t TotalFibers();
    static uint64_t GetFiberId();

private:
    uint64_t m_id = 0;
    uint32_t m_stacksize = 0;
    State m_state = READY;
    ucontext_t m_ctx;
    void* m_stack = nullptr;

    std::function<void()> m_cb;
    bool m_runInScheduler;  // 本协程是否参与调度器调度 
};



#endif