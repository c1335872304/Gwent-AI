#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <vector>

#include "gwent/engine/task.hpp"

namespace gwent {

class TaskQueue {
public:
    void push_back(Task task);
    void push_front(Task task);
    [[nodiscard]] std::optional<Task> pop_front();
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::vector<Task> take_all();
    void append_all(std::vector<Task> tasks);
    void clear() noexcept;

private:
    std::deque<Task> tasks_;
};

}  // namespace gwent
