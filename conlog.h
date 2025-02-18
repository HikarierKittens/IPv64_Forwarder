#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <stdexcept>

template <typename T>
class SemaphoreQueue {
public:
    SemaphoreQueue() = default;
    ~SemaphoreQueue() = default;

    void Enqueue(T value) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _queue.push(std::move(value));
        }
        _condition.notify_one();
    }

    T Dequeue() {
        std::unique_lock<std::mutex> lock(_mutex);
        _condition.wait(lock, [this] { return !_queue.empty(); });
        T value = std::move(_queue.front());
        _queue.pop();
        return value;
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(_mutex);
        std::queue<T> empty;
        std::swap(_queue, empty);
    }

    bool HasPendingTasks() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return !_queue.empty();
    }

private:
    mutable std::mutex _mutex;
    std::queue<T> _queue;
    std::condition_variable _condition;
};
#ifndef CONLOG_H
#define CONLOG_H

#include <string>
#include <winsock2.h>

void Log(const std::string& message);

void LogSocketError(int errorCode) {
    switch (errorCode) {
        case WSAECONNABORTED:
            Log("连接已被一方终止 (WSAECONNABORTED, 10053)");
            break;
        // 可以在这里添加更多的错误处理
        default:
            Log("Socket error: " + std::to_string(errorCode));
            break;
    }
}

#endif // CONLOG_H
