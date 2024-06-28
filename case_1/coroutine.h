#ifndef _COROUTINE__H_
#define _COROUTINE__H_

#include <iostream>
#include <memory>
#include <atomic>
#include <functional>
#include <cassert>
#include <ucontext.h>

class Fiber : public std::enable_shared_from_this<Fiber> {  // 通过shared_from_this()方法获取指向调用对象的shared_ptr<->对比this指针, 可以安全地生成一个指向自身的std::shared_ptr.
public:
    typedef std::shared_ptr<Fiber> ptr;


    //xc三种状态
    enum State {
        READY,   //刚刚创建或yield之后的状态
        RUNNING, //resume之后的状态
        TERM     //回调函数执行完之后的状态
    };

private:
    Fiber();  // 默认构造函数用于创建线程的第一个协程，主协程
    // 这个协程只能有GetThis方法调用,所以是私有

public:
    //构造函数，用于创建子协程(用于执行入口函数)

    // cd 协程的入口函数
        //std::function<void()> 一个函数对象,接收0个参数且没有返回值
    // stacksize 栈空间的大小
    // scheduler 是否参与调度器调度

    Fiber(std::function<void()> cd, size_t stacksize = 0, bool run_in_schedular = true);

    ~Fiber(); // 析构函数

    void reset(std::function<void()> cd); //重置协程状态和入口函数，复用栈空间


    void resume(); // 当前协程让出执行权, 由主协程执行resume()，来切换到主协程
    void yield();

    // 获取协程的id
    uint64_t getId() const {return m_id;}

    State getState() const {return m_state;} //获取协程状态

public:
    // 类中的static成员函数，可以在不创建对象的情况下调用，并且不能访问类的非静态成员变量

    static void SetThis(Fiber *f); //设置当前正在运行的协程，即设置t_fiber的值

    static std::shared_ptr<Fiber> GetThis(); // 返回当前线程正在执行的协程
	// 如果还未创建协程则创建第一个协程作为主协程，负责调度子协程
	// 其他子协程结束时都要且回到主协程，由主协程选择新的子协程进行resume
	// 应该首先执行该方法初始化主协程

    static uint64_t TotalFibers(); //总协程数

    static void MainFunc(); //协程入口函数

    static uint64_t GetFiberId(); //获取当前协程的id


private:
    uint64_t m_id = 0; //协程id
    uint32_t m_stacksize = 0; // 协程的栈空间大小，独立栈

    State m_state = READY; //协程的状态

    ucontext_t m_ctx; //协程上下文

    void* m_stack = nullptr; // 协程地址

    std::function<void()> m_cd; // 协程入口函数
    bool m_runInScheduler;
};

#endif