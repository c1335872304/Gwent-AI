#include "gwent/rl/strategic_reward.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

bool near(float a, float b, float eps = 1.0e-5F) {
    return std::fabs(a - b) <= eps;
}

void test_round_one_resource_efficiency() {
    const gwent::rl::StrategicRoundRewardParams p{};

    // Winner spends one fewer card: strong positive resource win.
    assert(near(gwent::rl::round_efficiency_reward(p, 1, 4, 5, 20), 0.15F));
    // Even-card win. Large margin gets no close-win bonus.
    assert(near(gwent::rl::round_efficiency_reward(p, 1, 5, 5, 15), 0.10F));
    // Same resource cost, one-point win receives the small efficiency tiebreak.
    assert(near(gwent::rl::round_efficiency_reward(p, 1, 5, 5, 1), 0.12F));
    // R1 may accept paying one card to win.
    assert(near(gwent::rl::round_efficiency_reward(p, 1, 6, 5, 15), 0.05F));
    // Paying two cards for R1 is strategically bad even though the round was won.
    assert(near(gwent::rl::round_efficiency_reward(p, 1, 7, 5, 15), -0.03F));
}

void test_round_two_is_stricter() {
    const gwent::rl::StrategicRoundRewardParams p{};

    assert(near(gwent::rl::round_efficiency_reward(p, 2, 4, 5, 20), 0.15F));
    assert(near(gwent::rl::round_efficiency_reward(p, 2, 5, 5, 15), 0.07F));
    assert(near(gwent::rl::round_efficiency_reward(p, 2, 5, 5, 1), 0.09F));
    assert(near(gwent::rl::round_efficiency_reward(p, 2, 6, 5, 15), -0.04F));
    assert(near(gwent::rl::round_efficiency_reward(p, 2, 7, 5, 15), -0.15F));
}

void test_round_three_has_no_resource_shaping() {
    const gwent::rl::StrategicRoundRewardParams p{};
    assert(near(gwent::rl::round_efficiency_reward(p, 3, 1, 8, 1), 0.0F));
    assert(near(gwent::rl::round_efficiency_reward(p, 3, 8, 1, 50), 0.0F));
}

}  // namespace

int main() {
    test_round_one_resource_efficiency();
    test_round_two_is_stricter();
    test_round_three_has_no_resource_shaping();
    std::cout << "strategic reward tests passed\n";
    return 0;
}
