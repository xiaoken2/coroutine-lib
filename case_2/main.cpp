#include "scheduler.h"

static unsigned int test_number;
std::mutex mutex_cout;

void task() {
    {
        std::lock_guard<std::mutex> lock(mutex_cout);
        std::cout << "task" << test_number++ << " is under processing in thread: " << Scheduler::GetThreadId() << std::endl;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(1000));
}

int main(int argc, char const *argv[]) {
    std::cout << "scheduler begins to test\n";
    {
        std::shared_ptr<Scheduler> scheduler = std::make_shared<Scheduler> (3, true, "scheduler_1");

        scheduler->start();
        sleep(8);

        std::cout << "begin post\n";

        for (int i = 0; i < 5; i++) {
            std::shared_ptr<Fiber> fiber = std::make_shared<Fiber>(task);
            scheduler->scheduleLock(fiber);
        }

        sleep(8);

        for (int i = 0; i < 20; i++) {
            std::shared_ptr<Fiber> fiber = std::make_shared<Fiber>(task);
            scheduler->scheduleLock(fiber);
        }

        sleep(4);

        scheduler->stop();
    }

    return 0;
}

