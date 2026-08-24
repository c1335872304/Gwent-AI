#include "gwent/engine/task_queue.hpp"

#include <utility>

namespace gwent {

void TaskQueue::push_back(Task task) {
    tasks_.push_back(std::move(task));
}

void TaskQueue::push_front(Task task) {
    tasks_.push_front(std::move(task));
}

std::optional<Task> TaskQueue::pop_front() {
    if (tasks_.empty()) {
        return std::nullopt;
    }
    Task task = std::move(tasks_.front());
    tasks_.pop_front();
    return task;
}

bool TaskQueue::empty() const noexcept {
    return tasks_.empty();
}

std::size_t TaskQueue::size() const noexcept {
    return tasks_.size();
}

std::vector<Task> TaskQueue::take_all() {
    std::vector<Task> tasks;
    tasks.reserve(tasks_.size());
    while (!tasks_.empty()) {
        tasks.push_back(std::move(tasks_.front()));
        tasks_.pop_front();
    }
    return tasks;
}

void TaskQueue::append_all(std::vector<Task> tasks) {
    for (Task& task : tasks) {
        tasks_.push_back(std::move(task));
    }
}

void TaskQueue::clear() noexcept {
    tasks_.clear();
}

}  // namespace gwent
