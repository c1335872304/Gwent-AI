#pragma once

#include <cstddef>

#include "gwent/core/enums.hpp"
#include "gwent/core/ids.hpp"

namespace gwent {

struct Location {
    PlayerId side = kPlayerZero;
    Zone zone = Zone::Deck;
    std::size_t index = 0;

    friend bool operator==(const Location&, const Location&) = default;
};

}  // namespace gwent
