#include "scheduler.h"

// 全局变量（线程局部变量）
// 调度器：由同一个调度器下的所有线程共有
static thread_local Scheduler* t_scheduler = nullptr;  //指向当前线程的调度器实例
static thread_local Fiber* t_scheduler_fiber = nullptr; // 指向当前线程的调度器协程

// 当前线程的线程id
// 主线程之外的线程将在创建工作线程后修改
static thread_local int s_thread_id = 0;  // 保存当前线程的id


// 获取调度器指针
Scheduler* Scheduler::GetThis() {
    return t_scheduler;
}

void Scheduler::SetThis() {
    t_scheduler = this;
}

Fiber* Scheduler::GetSchedulerFiber() {
    return t_scheduler_fiber;
}

int Scheduler::GetThreadId() {
    return s_thread_id;
}

void Scheduler::SetThreadId(int thread_id) {
    s_thread_id = thread_id;
}


Scheduler::Scheduler(size_t threads, bool use_caller, const std::string &name):
m_useCaller(use_caller), m_name(name){
    assert(threads > 0);
    assert(Scheduler::GetThis() == nullptr);

    tickler = 0;

    // 设置调度器指针
    SetThis();

    // 调度器所在的协程id
    m_rootThread = 0;

    // 主协程参与执行任务
    if (use_caller) {
        threads--;

        // 创建当前协程为主协程
        Fiber::GetThis();

        // 创建调度协程并设置到m_rootFiber
            // 调度协程第三个参数设置为false->yield时，调度协程应该返回主协程
        m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false));

        // 设置调度协程指针
        t_scheduler_fiber = m_rootFiber.get();

        // 加入线程池的线程id数组
        m_threadIds.push_back(m_rootThread);
        
    }

    // 还需要创建的额外线程
    m_threadCount = threads;
}

Scheduler::~Scheduler() {}

// 初始化调度线程池
    // 如果caller线程只进行调度，caller启动工作线程后，发布任务，然后使用tickle()或stop()启动所有工作线程执行任务

void Scheduler::start() {
    std::cout << "Scheduler starts" << std::endl;
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_stopping) {
        std::cerr << "Scheduler is stopped" << std::endl;
        return;
    }

    assert(m_threads.empty());
    m_threads.resize(m_threadCount);

    for (size_t i = 0; i < m_threadCount; i++) {
        m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this), m_name + "_" + std::to_string(i)));

        m_threadIds.push_back(m_threads[i]->getId());
    }
    std::cout << "Scheduler start() ends" << std::endl;
}



void Scheduler::run() {
    std::cout << "Scheduler::run() starts in thread: " << GetThreadId() << std::endl;

    SetThis();

    if (GetThreadId() != m_rootThread) {
        assert(t_scheduler_fiber == nullptr);
        // 创建主协程并将其设置到调度协程指针
        t_scheduler_fiber = Fiber::GetThis().get();
    }

    std::shared_ptr<Fiber> idle_fiber = std::make_shared<Fiber>(std::bind(&Scheduler::idle, this));
    std::shared_ptr<Fiber> cb_fiber;

    SchedulerTask task;

    while(true) {
        task.reset();
        bool tickle_me = false;
        // mute范围
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_tasks.begin();

            // 遍历所有的调度任务
            while (it != m_tasks.end()) {
                // 指定了调度线程，但是不是在当前线程上调度，然后就跳过这个任务，继续下一个
                if (it->thread != -1 && it->thread != GetThreadId()) {
                    it++;
                    continue;
                }

                // 发现可执行任务
                assert(it->fiber || it->cb);
                if (it->fiber) {
                    assert(it->fiber->getState() == Fiber::READY);
                }

                // 取出任务
                task = *it;
                m_tasks.erase(it);
                m_activateThreadCount++;
                break;
            }

            tickle_me = tickle_me || (it != m_tasks.end());
        }

        if (tickle_me) {
            tickle();
        }

        //3. 执行任务
        // 任务为协程任务
        if (task.fiber) {
            task.fiber->resume();
            m_activateThreadCount--;
            task.reset();
        } else if (task.cb) {  // 任务为函数任务
            if (cb_fiber) {
                cb_fiber->reset(task.cb);
            } else {
                cb_fiber.reset(new Fiber(task.cb));
            }
            cb_fiber->resume();
            m_activateThreadCount--;
            task.reset();
        } else {  // 4. 未取出任务->任务为空->切换到idle协程
            // 调度器已经关闭
            if (m_stopping == true) break;

            // 运行idle协程
            m_idleThreadCount++;
            idle_fiber->resume();
            m_idleThreadCount--;
        }
    }

    std::cout << "Scheduler::run() ends in thread: " << GetThreadId() << std::endl;
}

void Scheduler::stop() {
    std::cout << "Scheduler::stop() starts in thread: " << GetThreadId() << std::endl;

    if (stopping()) {
        return;
    }

    // 只能由调度器所在的线程发起stop
    assert(GetThreadId() == m_rootThread);

    // 所有线程开始工作
    for (size_t i = 0; i < m_threadCount; i++) {
        tickle();
    }

    if (m_rootFiber) {
        tickle();
    }

    // 不再添加任务->当任务为0时工作线程不在进行idle而是退出
    m_stopping = true;
    // 调度器所在的线程开始处理任务
        // 在use caller情况下，调度器协程结束时，应该返回caller协程
    if (m_rootFiber) {
        m_rootFiber->resume();
        std::cout << "m_rootFiber ends in thread: " << GetThreadId() << std::endl;
    }

    std::vector<std::shared_ptr<Thread>> thrs;
    {
    std::lock_guard<std::mutex> lock(m_mutex);
    thrs.swap(m_threads);
    }

    for (auto &i : thrs) {
        i->join();
    }
    std::cout << "Scheduler::stop() ends in thread: " << GetThreadId() << std::endl;
}

void Scheduler::tickle(){
    tickler++;
}

void Scheduler::idle() {
    while (true) {
        std::cout << "resume idle(), sleeping in thread: " << GetThreadId() << std::endl;

        while (tickler == 0 && m_stopping == false) {
            std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        }

        tickler--;
        std::shared_ptr<Fiber> curr = Fiber::GetThis();
        auto raw_ptr = curr.get();
        curr.reset();
        raw_ptr->yield();
    }
}

// 发布线程任务
void Scheduler::scheduleLock(std::shared_ptr<Fiber> fc, int thread_id) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        SchedulerTask task;

        task.fiber = fc;
        task.cb = nullptr;
        task.thread = thread_id;

        m_tasks.push_back(task);
    }
    tickle();
}

// 发布函数任务
void Scheduler::scheduleLock(std::function<void()> fc, int thread_id) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        SchedulerTask task;

        task.fiber = nullptr;
        task.cb = fc;
        task.thread = s_thread_id;

        m_tasks.push_back(task);
    }
    tickle();
}


