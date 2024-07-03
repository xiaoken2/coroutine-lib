#include <vector>
#include <functional>

#include "coroutine.h"

// 主协程运行协程调度器：支持添加调度任务以及运行调度任务，采用先来先服务算法

// 协程调度器
class Scheduler {
public:
    
    // 添加协程调度任务
    void schedule(std::shared_ptr<Fiber> task) {
        m_tasks.push_back(task);
    }

    void run () {
        std::cout << "number" << m_tasks.size() << std::endl;

        std::shared_ptr<Fiber> task;
        auto it = m_tasks.begin();
        while (it != m_tasks.end()) {
            // 迭代器本身也是指针
            task = *it; 
            // 由主协程切换到子协程，子协程函数运行完毕以后自动切换到主协程
            task->resume();
            it++;
        }

        m_tasks.clear();

    }

private:
    // 任务队列
    std::vector<std::shared_ptr<Fiber>> m_tasks;
};

void test_fiber(int i) {
    std::cout << "hellow world " << i << std::endl;
}

int main() {
    // 初始化当前线程的主协程
    Fiber::GetThis();

    //
    Scheduler sc;

    // 
    for (auto i = 0; i < 20; i++) {
        std::shared_ptr<Fiber> fiber = std::make_shared<Fiber>(std::bind(test_fiber, i));
        sc.schedule(fiber);
    }

    sc.run();

    return 0;

}