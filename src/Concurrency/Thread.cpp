#include "Thread.h"

namespace GST {
namespace BASE {
Thread::Thread() {
    data = nullptr;
}
Thread::~Thread() {
    if (thread) {
        pthread_join(thread, nullptr);
        thread = 0;
    }
    if (data) {
        delete data;
        data = nullptr;
    }
}
void Thread::join() {
    if (thread) {
        pthread_join(thread, nullptr);
        thread = 0;
    }
}

void Thread::detach() {
    if (thread) {
        pthread_detach(thread);
        thread = 0;
    }
}

}
}