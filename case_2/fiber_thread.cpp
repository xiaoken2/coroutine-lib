#include "fiber_thread.h"

static thread_local std::atomic<int> s_working_thread_id{1};

Thread::Thread(std::function<void()> cb, const std::string & name) : m_name(name){
    m_thread_id = s_working_thread_id++;
    m_thread = std::thread([this, cb]() {Scheduler::SetThreadId(this->m_thread_id); cb();});
}

Thread::~Thread() {
    if (m_thread.joinable()) {
        m_thread.join();
    }
}