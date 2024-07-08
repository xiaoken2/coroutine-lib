#include <iostream>
#include "coroutine.h"
/*
typedef struct ucontext_t {
    struct ucontext_t *uc_link;

    sigset_t uc_sigmask;
    
    stack_t uc_stack;

    mcontext_t uc_mcontext;

} ucontext_t;
*/

int getcontext(ucontext_t *ucp);
int setcontext(const ucontext_t *ucp);

void makecontext(ucontext_t *ucp, void (*func)(), int argc, ...);

int swapcontext(ucontext_t *oucp, const ucontext_t *ucp);

// 线程局部变量记录一个线程的协程控制信息
// 当前线程正在运行的协程
static thread_local Fiber* t_fiber = nullptr;

// 当前线程的主协程，必须用shared_ptr持有，不然会在默认构造中创建中消失
static thread_local std::shared_ptr<Fiber> t_thread_fiber = nullptr;

// 当前线程的协程数量
static thread_local int s_fiber_count = 0;

//当前线程的协程id
static thread_local int s_fiber_id = 0;

void Fiber::SetThis(Fiber *f) {
    t_fiber = f;
}

// 返回当前线程正在执行的协程
std::shared_ptr<Fiber> Fiber::GetThis() {
    if (t_fiber) return t_fiber->shared_from_this();  //返回指向当前对象的shared_ptr

    // 如果当前线程还未创建协程，则创建线程的主协程
        // Fiber是私有的，不能使用make_shared()
    std::shared_ptr<Fiber> main_fiber(new Fiber());
    t_thread_fiber = main_fiber;

    assert(t_fiber == main_fiber.get());

    return t_fiber->shared_from_this();

}

Fiber::Fiber() {
    SetThis(this); // 设置正在运行的协程为此协程
    m_state = RUNNING;
    
    if (getcontext(&m_ctx)) {
        std::cerr << "Fiber() failed\n";
        exit(0);
    }

    s_fiber_count++;
    m_id = s_fiber_id++;
    std::cout << "Fiber(): main id = " << m_id << std::endl;

}

Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler):
m_cb(cb), m_runInScheduler(run_in_scheduler) {
    m_state = READY;
    m_stacksize = stacksize ? stacksize : 128000;
    m_stack = malloc(m_stacksize);

    if (getcontext(&m_ctx)) {
        std::cerr << "Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler) failed\n";
        exit(0);
    }

    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    makecontext(&m_ctx, &Fiber::MainFunc, 0);

    m_id = s_fiber_id++;
    s_fiber_count++;
    std::cout << "Fiber(): child id = " << m_id << std::endl;
}

Fiber::~Fiber() {
    if (m_stack) {
        free(m_stack);
    }
}

// 重置协程状态和入口函数，重用协程
void Fiber::reset(std::function<void()> cb) {
    assert(m_stack != nullptr);
    assert(m_state == TERM);

    m_cb = cb;
    m_state = READY;

    if (getcontext(&m_ctx)) {
        std::cerr << "reset() failed\n";
        exit(0);
    }

    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    makecontext(&m_ctx, &Fiber::MainFunc, 0);
}

void Fiber::resume() {
    assert(m_state == READY);
    SetThis(this);
    m_state = RUNNING;
    
    if (m_runInScheduler) {
        if (swapcontext(&(Scheduler::GetSchedulerFiber()->m_ctx), &m_ctx)) {
            std::cerr << "resum() to GetSchedulerFiber faild\n";
            pthread_exit(NULL);
        }
    } else {
        if (swapcontext(&(t_thread_fiber->m_ctx), &m_ctx)) {
            std::cerr << "resum() to t_thread_fiber faild\n";
        }
    }
}

void Fiber::yield() {
    assert(m_state == RUNNING || m_state == TERM);

    if (m_state != TERM) {
        m_state = READY;
    }

    if (m_runInScheduler) {
        SetThis(Scheduler::GetSchedulerFiber());
        if (swapcontext(&m_ctx, &(Scheduler::GetSchedulerFiber()->m_ctx))) {
            std::cerr << "yield() to to GetSchedulerFiber faild\n";
            pthread_exit(NULL);
        }
    } else {
        SetThis(Scheduler::GetSchedulerFiber());
        if (swapcontext(&m_ctx, &(t_thread_fiber->m_ctx))) {
            std::cerr << "yield() to t_thread_fiber failed\n";
            pthread_exit(NULL);
        }
    }
}

void Fiber::MainFunc() {
    
    std::shared_ptr<Fiber> curr = GetThis();

    assert(curr != nullptr);

    curr->m_cb();

    curr->m_cb = nullptr;
    curr->m_state =TERM;

    auto raw_ptr = curr.get();
    curr.reset();

    raw_ptr->yield();
}




