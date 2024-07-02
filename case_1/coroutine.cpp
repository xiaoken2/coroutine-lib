#include "coroutine.h"
#include <iostream> 

/*
typedef struct ucontext_t {
    struct ucontext_t *uc_link;

    sigset_t uc_sigmask;

    stack_t uc_stack;

    mcontext_t uc_mcontext;
} ucontext_t;
*/

//获取协程的上下文信息
int getcontext(ucontext_t *ucp); 


// 恢复ucp指向的上下文信息
int setcontext(const ucontext_t *ucp);


void makecontext(ucontext_t *ucp, void (*func)(), int argc, ...);
// 执行完makecontext以后，ucp和func就绑定在一起了，调用setcontext或者swapcontext激活ucp时，func就会被运行

// 恢复ucp指向的上下文，同时将当前的上下文存储到oucp中（切换协程）
// 不会返回，而是调到ucp上下文对应的函数中执行，相当于调用了函数
int swapcontext(ucontext_t *oucp, const ucontext_t *ucp);

// 当前线程正在运行的协程
static thread_local Fiber* t_fiber = nullptr;

// 当前线程的主协程
static thread_local std::shared_ptr<Fiber> t_thread_fiber = nullptr;

// 当前线程的协程数量
static thread_local int s_fiber_count = 0;

// 当前线程的协程的id
static thread_local int s_fiber_id;

void Fiber::SetThis(Fiber *f) {
    t_fiber = f;
}

// 返回当前线程正在执行的协程

std::shared_ptr<Fiber> Fiber::GetThis() {
    if (t_fiber) {
        // 返回当前对象的shared_ptr
        return t_fiber->shared_from_this();
    }

    std::shared_ptr<Fiber> main_fiber(new Fiber());

    // 确认主协程的默认构造函数成功运行，get智能指针对象，可以获取原始指针
    assert(t_fiber == main_fiber.get());

    // 设置指向主协程的智能指针
    t_thread_fiber = main_fiber;

    return t_fiber->shared_from_this();

}

//创建线程的主协程，负责调度子协程

Fiber::Fiber() {
    SetThis(this); //设置正在运行的协程为此协程
    m_state = RUNNING;

    //获取当前协程的上下文
    if (getcontext(&m_ctx)) {
        std::cerr << "Fiber() failed\n";
        exit(0);
    }

    // 更新当前线程的协程控制信息
    s_fiber_count++;
    m_id = s_fiber_id++;
    std::cout << "Fiber(): main id = " << m_id << std::endl;

}

//创建线程的子协程
Fiber::Fiber(std::function<void()>cb, size_t stacksize, bool run_in_scheduler):
m_cb(cb) {
    m_state = READY;
    
    // 为子协程分配栈空间
    m_stacksize = stacksize ? stacksize : 128000;
    m_stack = malloc(m_stacksize);

    if (getcontext(&m_ctx)) {
        std::cerr << "Fiber(std::function<void()>cb, size_t stacksize, bool run_in_scheduler) failed\n";
        exit(0);
    }

    // 修改为子协程上下文
    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    // 将子协程上下文和函数入口函数绑定
    makecontext(&m_ctx, &Fiber::MainFunc, 0);

    // 更新当前线程的协程控制信息
    m_id =  s_fiber_id++;
    s_fiber_count++;
    std::cout << "Fiber(): id = " << m_id << std::endl;
}

Fiber::~Fiber() {
    if (m_stack) {
        free(m_stack);
    }
}

// 重置协程的状态和入口函数，重用协程栈，不用重新创建栈

void Fiber::reset(std::function<void()> cb) {
    assert(m_stack != nullptr);
    assert(m_state == TERM); // 为了简化状态管理，强制只有TERM状态的协程才可以重置

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

// 在非对称协程中，执行resume时当前执行的协程一定是主协程
//  将子协程切换到执行状态，和主协程进行交换

void Fiber::resume() {
    assert(m_state == READY);
    SetThis(this);
    m_state = RUNNING;

    if (swapcontext(&(t_thread_fiber->m_ctx), &m_ctx)) {
        std::cerr << "resume() failed\n";
        exit(0);
    }
}

// 执行yield时，当前执行的一定是子协程
// 将子协程让出执行权，切换到主协程
void Fiber::yield() {
    // 协程运行完以后会自动yield一次，用于回到主协程，此时状态为TERM
    assert(m_state == RUNNING || m_state == TERM);
    SetThis(t_thread_fiber.get());
    if (m_state != TERM) {
        m_state = READY;
    }

    if (swapcontext(&m_ctx, &(t_thread_fiber->m_ctx))) {
        std::cerr << "yield() failed\n";
        exit(0);
    }
}

// 协程入口函数的封装：1.调用入口函数；2.调用yield函数返回主协程
void Fiber::MainFunc() {
    // 返回当前线程正在执行的协程
        // shared_from_this() 使用计数加1
    std::shared_ptr<Fiber> curr = GetThis();
    assert(curr != nullptr);
    
    // 调用真正的函数入口
    curr->m_cb();

    // 运行完毕修改协程的状态
    curr->m_cb = nullptr;
    curr->m_state = TERM;

    auto raw_ptr = curr.get();

    curr.reset();

    raw_ptr->yield();
}

