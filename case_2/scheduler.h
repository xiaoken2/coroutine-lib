#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include <vector>
#include <mutex>

#include "coroutine.h"
#include "fiber_thread.h"

class Fiber;

class Thread;

class Scheduler {
public:
    Scheduler(size_t threads = 1, bool use_caller = true, const std::string& name = "Scheduler");

    virtual ~Scheduler();

    const std::string& getName() const {return m_name;}

private:
    // 协程调度器名称
    std::string m_name;
    // 互斥锁
    std::mutex m_mutex;

    // 线程池
    std::vector<std::shared_ptr<Thread>> m_thread;

};

#endif