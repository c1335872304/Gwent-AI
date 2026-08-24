#pragma once

#include <algorithm>
#include <cmath>

namespace gwent::rl {

struct StrategicRoundRewardParams {
    float round1_win_base = 0.10F;
    float round1_card_cost_weight = 0.05F;
    float round1_excess_card_penalty = 0.03F;
    float round2_win_base = 0.07F;
    float round2_card_cost_weight = 0.08F;
    float round2_excess_card_penalty = 0.03F;
    float close_round_win_bonus = 0.02F;
    int close_round_win_margin_cap = 15;
};

[[nodiscard]] inline float close_margin_bonus(
    const StrategicRoundRewardParams& params,
    int margin) noexcept {
    const int cap = std::max(1, params.close_round_win_margin_cap);
    const int positive_margin = std::max(1, margin);
    if (positive_margin >= cap || params.close_round_win_bonus == 0.0F) {
        return 0.0F;
    }
    if (cap == 1) {
        return std::abs(params.close_round_win_bonus);
    }
    const float fraction = static_cast<float>(cap - positive_margin) / static_cast<float>(cap - 1);
    return std::abs(params.close_round_win_bonus) * std::clamp(fraction, 0.0F, 1.0F);
}

// card_cost = winner cards spent this round - loser cards spent this round.
// Negative means the winner preserved more cards (earned card advantage).
// Positive means the winner paid card disadvantage for the round.
[[nodiscard]] inline float round_efficiency_reward(
    const StrategicRoundRewardParams& params,
    int round_no,
    int winner_cards_spent,
    int loser_cards_spent,
    int score_margin) noexcept {
    if (round_no >= 3 || round_no <= 0) {
        return 0.0F;
    }

    const int card_cost = winner_cards_spent - loser_cards_spent;
    float reward = 0.0F;
    bool margin_eligible = false;

    if (round_no == 1) {
        reward = params.round1_win_base
            - params.round1_card_cost_weight * static_cast<float>(card_cost);
        if (card_cost >= 2) {
            reward -= std::abs(params.round1_excess_card_penalty)
                * static_cast<float>(card_cost - 1);
        }
        // R1 may rationally spend one extra card for initiative. For even-card
        // or one-card-cost wins, a close score is a small efficiency tiebreaker.
        margin_eligible = card_cost == 0 || card_cost == 1;
    } else if (round_no == 2) {
        reward = params.round2_win_base
            - params.round2_card_cost_weight * static_cast<float>(card_cost);
        if (card_cost >= 1) {
            reward -= std::abs(params.round2_excess_card_penalty)
                * static_cast<float>(card_cost);
        }
        // R2 is stricter: only an even-card win receives a score-margin bonus.
        margin_eligible = card_cost == 0;
    }

    if (margin_eligible) {
        reward += close_margin_bonus(params, score_margin);
    }
    return reward;
}

}  // namespace gwent::rl
