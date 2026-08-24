#include "gwent/core/enums.hpp"

namespace gwent {

std::string_view to_string(Zone zone) noexcept {
    switch (zone) {
        case Zone::Deck: return "DECK";
        case Zone::Hand: return "HAND";
        case Zone::Stay: return "STAY";
        case Zone::Melee: return "MELEE";
        case Zone::Ranged: return "RANGED";
        case Zone::Cemetery: return "CEMETERY";
        case Zone::Banished: return "BANISHED";
        case Zone::Leader: return "LEADER";
    }
    return "UNKNOWN";
}

std::string_view to_string(CardType type) noexcept {
    switch (type) {
        case CardType::Unit: return "UNIT";
        case CardType::Special: return "SPECIAL";
        case CardType::Artifact: return "ARTIFACT";
        case CardType::Leader: return "LEADER";
        case CardType::Stratagem: return "STRATAGEM";
    }
    return "UNKNOWN";
}

std::string_view to_string(Faction faction) noexcept {
    switch (faction) {
        case Faction::NorthernRealms: return "NORTHERN_REALMS";
        case Faction::Nilfgaard: return "NILFGAARD";
        case Faction::Monsters: return "MONSTERS";
        case Faction::Scoiatael: return "SCOIATAEL";
        case Faction::Skellige: return "SKELLIGE";
        case Faction::Syndicate: return "SYNDICATE";
        case Faction::Neutral: return "NEUTRAL";
    }
    return "UNKNOWN";
}

std::string_view to_string(CardStatus status) noexcept {
    switch (status) {
        case CardStatus::Shield: return "shield";
        case CardStatus::Infiltration: return "infiltration";
        case CardStatus::Spying: return "spying";
        case CardStatus::Locked: return "locked";
        case CardStatus::Resilience: return "resilience";
        case CardStatus::Doomed: return "doomed";
        case CardStatus::Veil: return "veil";
        case CardStatus::Poison: return "poison";
        case CardStatus::Bleeding: return "bleeding";
        case CardStatus::Vitality: return "vitality";
        case CardStatus::Bounty: return "bounty";
        case CardStatus::Immune: return "immune";
        case CardStatus::Defender: return "defender";
        case CardStatus::Rupture: return "rupture";
    }
    return "unknown";
}

std::string_view to_string(CardUseInfo use_info) noexcept {
    switch (use_info) {
        case CardUseInfo::MyPlace: return "MY_PLACE";
        case CardUseInfo::EnemyPlace: return "ENEMY_PLACE";
        case CardUseInfo::AnyPlace: return "ANY_PLACE";
        case CardUseInfo::MyRow: return "MY_ROW";
        case CardUseInfo::EnemyRow: return "ENEMY_ROW";
        case CardUseInfo::AnyRow: return "ANY_ROW";
    }
    return "UNKNOWN";
}

std::string_view to_string(MatchStatus status) noexcept {
    switch (status) {
        case MatchStatus::NotStarted: return "NOT_STARTED";
        case MatchStatus::Running: return "RUNNING";
        case MatchStatus::Finished: return "FINISHED";
    }
    return "UNKNOWN";
}

std::string_view to_string(MatchPhase phase) noexcept {
    switch (phase) {
        case MatchPhase::NotStarted: return "NOT_STARTED";
        case MatchPhase::Playing: return "PLAYING";
        case MatchPhase::Finished: return "FINISHED";
    }
    return "UNKNOWN";
}

std::string_view to_string(EnginePhase phase) noexcept {
    switch (phase) {
        case EnginePhase::NotStarted: return "NOT_STARTED";
        case EnginePhase::ActionWindow: return "ACTION_WINDOW";
        case EnginePhase::Resolving: return "RESOLVING";
        case EnginePhase::AwaitingChoice: return "AWAITING_CHOICE";
        case EnginePhase::TurnClosing: return "TURN_CLOSING";
        case EnginePhase::RoundClosing: return "ROUND_CLOSING";
        case EnginePhase::Finished: return "FINISHED";
    }
    return "UNKNOWN";
}

std::optional<Zone> zone_from_string(std::string_view value) noexcept {
    if (value == "DECK") return Zone::Deck;
    if (value == "HAND") return Zone::Hand;
    if (value == "STAY") return Zone::Stay;
    if (value == "MELEE") return Zone::Melee;
    if (value == "RANGED") return Zone::Ranged;
    if (value == "CEMETERY") return Zone::Cemetery;
    if (value == "BANISHED") return Zone::Banished;
    if (value == "LEADER") return Zone::Leader;
    return std::nullopt;
}

std::optional<CardType> card_type_from_string(std::string_view value) noexcept {
    if (value == "UNIT") return CardType::Unit;
    if (value == "SPECIAL") return CardType::Special;
    if (value == "ARTIFACT") return CardType::Artifact;
    if (value == "LEADER") return CardType::Leader;
    if (value == "STRATAGEM") return CardType::Stratagem;
    return std::nullopt;
}

std::optional<Faction> faction_from_string(std::string_view value) noexcept {
    if (value == "NORTHERN_REALMS") return Faction::NorthernRealms;
    if (value == "NILFGAARD") return Faction::Nilfgaard;
    if (value == "MONSTERS") return Faction::Monsters;
    if (value == "SCOIATAEL") return Faction::Scoiatael;
    if (value == "SKELLIGE") return Faction::Skellige;
    if (value == "SYNDICATE") return Faction::Syndicate;
    if (value == "NEUTRAL") return Faction::Neutral;
    return std::nullopt;
}

std::optional<CardUseInfo> card_use_info_from_string(std::string_view value) noexcept {
    if (value == "MY_PLACE") return CardUseInfo::MyPlace;
    if (value == "ENEMY_PLACE") return CardUseInfo::EnemyPlace;
    if (value == "ANY_PLACE") return CardUseInfo::AnyPlace;
    if (value == "MY_ROW") return CardUseInfo::MyRow;
    if (value == "ENEMY_ROW") return CardUseInfo::EnemyRow;
    if (value == "ANY_ROW") return CardUseInfo::AnyRow;
    return std::nullopt;
}

std::optional<MatchStatus> match_status_from_string(std::string_view value) noexcept {
    if (value == "NOT_STARTED") return MatchStatus::NotStarted;
    if (value == "RUNNING") return MatchStatus::Running;
    if (value == "FINISHED") return MatchStatus::Finished;
    return std::nullopt;
}

std::optional<MatchPhase> match_phase_from_string(std::string_view value) noexcept {
    if (value == "NOT_STARTED") return MatchPhase::NotStarted;
    if (value == "PLAYING") return MatchPhase::Playing;
    if (value == "FINISHED") return MatchPhase::Finished;
    return std::nullopt;
}

}  // namespace gwent
