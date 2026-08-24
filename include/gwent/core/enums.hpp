#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace gwent {

enum class Zone : std::uint8_t {
    Deck,
    Hand,
    Stay,
    Melee,
    Ranged,
    Cemetery,
    Banished,
    Leader,
};

enum class CardType : std::uint8_t {
    Unit,
    Special,
    Artifact,
    Leader,
    Stratagem,
};

enum class Faction : std::uint8_t {
    NorthernRealms,
    Nilfgaard,
    Monsters,
    Scoiatael,
    Skellige,
    Syndicate,
    Neutral,
};

enum class CardStatus : std::uint8_t {
    Shield,
    Infiltration,
    Spying,
    Locked,
    Resilience,
    Doomed,
    Veil,
    Poison,
    Bleeding,
    Vitality,
    Bounty,
    Immune,
    Defender,
    Rupture,
};

enum class CardUseInfo : std::uint8_t {
    MyPlace,
    EnemyPlace,
    AnyPlace,
    MyRow,
    EnemyRow,
    AnyRow,
};

enum class MatchStatus : std::uint8_t {
    NotStarted,
    Running,
    Finished,
};

enum class MatchPhase : std::uint8_t {
    NotStarted,
    Playing,
    Finished,
};

// Internal engine-resolution phase. MatchPhase describes the broad match
// lifecycle; EnginePhase describes whether the rules core is currently open
// to player input or resolving an already submitted action/effect chain.
enum class EnginePhase : std::uint8_t {
    NotStarted,
    ActionWindow,
    Resolving,
    AwaitingChoice,
    TurnClosing,
    RoundClosing,
    Finished,
};

inline constexpr std::array<Zone, 2> kBattleZones{Zone::Melee, Zone::Ranged};

constexpr bool is_battle_zone(Zone zone) noexcept {
    return zone == Zone::Melee || zone == Zone::Ranged;
}

constexpr std::size_t row_index(Zone zone) {
    return zone == Zone::Melee ? 0U : 1U;
}

std::string_view to_string(Zone zone) noexcept;
std::string_view to_string(CardType type) noexcept;
std::string_view to_string(Faction faction) noexcept;
std::string_view to_string(CardStatus status) noexcept;
std::string_view to_string(CardUseInfo use_info) noexcept;
std::string_view to_string(MatchStatus status) noexcept;
std::string_view to_string(MatchPhase phase) noexcept;
std::string_view to_string(EnginePhase phase) noexcept;

std::optional<Zone> zone_from_string(std::string_view value) noexcept;
std::optional<CardType> card_type_from_string(std::string_view value) noexcept;
std::optional<Faction> faction_from_string(std::string_view value) noexcept;
std::optional<CardUseInfo> card_use_info_from_string(std::string_view value) noexcept;
std::optional<MatchStatus> match_status_from_string(std::string_view value) noexcept;
std::optional<MatchPhase> match_phase_from_string(std::string_view value) noexcept;

}  // namespace gwent
