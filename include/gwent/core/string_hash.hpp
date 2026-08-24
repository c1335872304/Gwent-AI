#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace gwent {

// Transparent hash for string-keyed unordered containers. This enables C++20
// heterogeneous lookup with std::string_view and avoids temporary std::string
// allocations on read-only hot paths.
struct TransparentStringHash {
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }
    [[nodiscard]] std::size_t operator()(const std::string& value) const noexcept {
        return (*this)(std::string_view{value});
    }
    [[nodiscard]] std::size_t operator()(const char* value) const noexcept {
        return (*this)(std::string_view{value});
    }
};

}  // namespace gwent
