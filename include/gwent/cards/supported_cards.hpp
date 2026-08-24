#pragma once

#include <string_view>
#include <vector>

#include "gwent/core/card_definition.hpp"
#include "gwent/engine/effect_registry.hpp"
#include "gwent/game/card_catalog.hpp"
#include "gwent/game/setup.hpp"

namespace gwent {
namespace supported_cards {

inline constexpr std::string_view kBloodScentId = "202185";
inline constexpr std::string_view kWhiteFrostId = "200055";
inline constexpr std::string_view kArmorerWorkshopId = "202497";
inline constexpr std::string_view kCrystalSkullId = "202493";
inline constexpr std::string_view kRegisRebornId = "203099";
inline constexpr std::string_view kUnseenElderId = "202889";
inline constexpr std::string_view kGiantCentipedeId = "202437";
inline constexpr std::string_view kDettlaffAepId = "202888";
inline constexpr std::string_view kLordRiptideId = "203265";
inline constexpr std::string_view kKatakanId = "132220";
inline constexpr std::string_view kVerenaId = "203284";
inline constexpr std::string_view kNaglfarId = "200301";
inline constexpr std::string_view kImlerithId = "201781";
inline constexpr std::string_view kGeelsId = "131102";
inline constexpr std::string_view kOzzrelId = "201698";
inline constexpr std::string_view kQueenOfTheNightId = "202221";
inline constexpr std::string_view kFlederId = "202229";
inline constexpr std::string_view kGarkainId = "202233";
inline constexpr std::string_view kBruxaId = "202230";
inline constexpr std::string_view kFeastOfBloodId = "202228";
inline constexpr std::string_view kAenElleConquerorId = "202614";
inline constexpr std::string_view kBloodscentedPredatorId = "202231";
inline constexpr std::string_view kWildHuntNavigatorId = "200026";
inline constexpr std::string_view kWildHuntRiderId = "132310";
inline constexpr std::string_view kWildHuntWarriorId = "132309";
inline constexpr std::string_view kWildHuntHoundId = "132402";
inline constexpr std::string_view kWildHuntBruiserId = "202611";
inline constexpr std::string_view kNaglfarCrewId = "202612";
inline constexpr std::string_view kNaglfarTaskmasterId = "202613";
inline constexpr std::string_view kAenElleAristocratId = "203160";
inline constexpr std::string_view kAenElleSlaveTraderId = "203161";
inline constexpr std::string_view kOberonKingId = "202603";
inline constexpr std::string_view kOberonInvaderId = "202604";
inline constexpr std::string_view kOberonConquerorId = "202605";
inline constexpr std::string_view kImlerithsWrathId = "132102";
inline constexpr std::string_view kApiarianPhantomId = "202609";
inline constexpr std::string_view kEredinBreaccGlasId = "202606";
inline constexpr std::string_view kWinterQueenId = "202608";
inline constexpr std::string_view kRedRidersId = "202610";
inline constexpr std::string_view kRedRidersLongFrostId = "202703";
inline constexpr std::string_view kRedRidersReplayId = "202704";
inline constexpr std::string_view kRedRidersBothRowsId = "203125";
inline constexpr std::string_view kAncientFogletId = "132302";
inline constexpr std::string_view kCaranthirGoldenChildId = "203159";
inline constexpr std::string_view kTirNaLiaId = "203158";
inline constexpr std::string_view kArdGaethId = "202607";
inline constexpr std::string_view kEkimmaraId = "132313";

inline constexpr std::string_view kBloodScentLeaderEffect = "supported.blood_scent.leader";
inline constexpr std::string_view kWhiteFrostLeaderEffect = "supported.white_frost.leader";
inline constexpr std::string_view kWhiteFrostCardPlayedEffect = "supported.white_frost.card_played";
inline constexpr std::string_view kArmorerWorkshopOrderEffect = "supported.armorer_workshop.order";
inline constexpr std::string_view kCrystalSkullOrderEffect = "supported.crystal_skull.order";
inline constexpr std::string_view kRegisDeployEffect = "supported.regis.deploy";
inline constexpr std::string_view kRegisDrainEffect = "supported.regis.drain";
inline constexpr std::string_view kRegisTurnEndEffect = "supported.regis.turn_end";
inline constexpr std::string_view kUnseenElderDeployEffect = "supported.unseen_elder.deploy";
inline constexpr std::string_view kUnseenElderTurnEndEffect = "supported.unseen_elder.turn_end";
inline constexpr std::string_view kGiantCentipedeDeployEffect = "supported.giant_centipede.deploy";
inline constexpr std::string_view kGiantCentipedeArmorBrokenEffect = "supported.giant_centipede.armor_broken";
inline constexpr std::string_view kDettlaffDeployEffect = "supported.dettlaff.deploy";
inline constexpr std::string_view kDettlaffOrderEffect = "supported.dettlaff.order";
inline constexpr std::string_view kDettlaffDeathblowEffect = "supported.dettlaff.deathblow";
inline constexpr std::string_view kLordRiptideDeployEffect = "supported.lord_riptide.deploy";
inline constexpr std::string_view kLordRiptideHandEndEffect = "supported.lord_riptide.hand_end";
inline constexpr std::string_view kKatakanDeployEffect = "supported.katakan.deploy";
inline constexpr std::string_view kKatakanOrderEffect = "supported.katakan.order";
inline constexpr std::string_view kKatakanBleedingAddedEffect = "supported.katakan.bleeding_added";
inline constexpr std::string_view kVerenaDeployEffect = "supported.verena.deploy";
inline constexpr std::string_view kVerenaBleedingAddedEffect = "supported.verena.bleeding_added";
inline constexpr std::string_view kNaglfarSpecialEffect = "supported.naglfar.special";
inline constexpr std::string_view kGeelsDeployEffect = "supported.geels.deploy";
inline constexpr std::string_view kImlerithDeployEffect = "supported.imlerith.deploy";
inline constexpr std::string_view kOzzrelDeployEffect = "supported.ozzrel.deploy";
inline constexpr std::string_view kQueenDeployEffect = "supported.queen_of_the_night.deploy";
inline constexpr std::string_view kFlederDeployEffect = "supported.fleder.deploy";
inline constexpr std::string_view kFlederBleedingAddedEffect = "supported.fleder.bleeding_added";
inline constexpr std::string_view kFlederTurnStartEffect = "supported.fleder.turn_start";
inline constexpr std::string_view kGarkainDeployEffect = "supported.garkain.deploy";
inline constexpr std::string_view kGarkainOrderEffect = "supported.garkain.order";
inline constexpr std::string_view kGarkainVampirePlayedEffect = "supported.garkain.vampire_played";
inline constexpr std::string_view kBruxaDeployEffect = "supported.bruxa.deploy";
inline constexpr std::string_view kBruxaOrderEffect = "supported.bruxa.order";
inline constexpr std::string_view kFeastOfBloodSpecialEffect = "supported.feast_of_blood.special";
inline constexpr std::string_view kAenElleConquerorDeployEffect = "supported.aen_elle_conqueror.deploy";
inline constexpr std::string_view kPredatorDeployEffect = "supported.bloodscented_predator.deploy";
inline constexpr std::string_view kWildHuntNavigatorDeployEffect = "supported.wild_hunt_navigator.deploy";
inline constexpr std::string_view kRiderDeployEffect = "supported.wild_hunt_rider.deploy";
inline constexpr std::string_view kWildHuntWarriorDeployEffect = "supported.wild_hunt_warrior.deploy";
inline constexpr std::string_view kWildHuntHoundTurnEndEffect = "supported.wild_hunt_hound.turn_end";
inline constexpr std::string_view kWildHuntBruiserDeployEffect = "supported.wild_hunt_bruiser.deploy";
inline constexpr std::string_view kNaglfarCrewDeployEffect = "supported.naglfar_crew.deploy";
inline constexpr std::string_view kNaglfarCrewTurnEndEffect = "supported.naglfar_crew.turn_end";
inline constexpr std::string_view kNaglfarTaskmasterDeployEffect = "supported.naglfar_taskmaster.deploy";
inline constexpr std::string_view kAenElleAristocratOrderEffect = "supported.aen_elle_aristocrat.order";
inline constexpr std::string_view kAenElleAristocratTurnEndEffect = "supported.aen_elle_aristocrat.turn_end";
inline constexpr std::string_view kAenElleSlaveTraderDeployEffect = "supported.aen_elle_slave_trader.deploy";
inline constexpr std::string_view kAenElleSlaveTraderInfusionEffect = "supported.aen_elle_slave_trader.infusion";
inline constexpr std::string_view kOberonKingDeployEffect = "supported.oberon_king.deploy";
inline constexpr std::string_view kOberonKingRoundStartEffect = "supported.oberon_king.round_start";
inline constexpr std::string_view kOberonInvaderDeployEffect = "supported.oberon_invader.deploy";
inline constexpr std::string_view kOberonInvaderRoundStartEffect = "supported.oberon_invader.round_start";
inline constexpr std::string_view kOberonConquerorDeployEffect = "supported.oberon_conqueror.deploy";
inline constexpr std::string_view kOberonConquerorCardPlayedEffect = "supported.oberon_conqueror.card_played";
inline constexpr std::string_view kImlerithsWrathSpecialEffect = "supported.imleriths_wrath.special";
inline constexpr std::string_view kApiarianPhantomOrderEffect = "supported.apiarian_phantom.order";
inline constexpr std::string_view kApiarianPhantomTurnEndEffect = "supported.apiarian_phantom.turn_end";
inline constexpr std::string_view kEredinDeployEffect = "supported.eredin.deploy";
inline constexpr std::string_view kWinterQueenTurnEndEffect = "supported.winter_queen.turn_end";
inline constexpr std::string_view kWinterQueenRoundEndEffect = "supported.winter_queen.round_end";
inline constexpr std::string_view kRedRidersSpecialEffect = "supported.red_riders.special";
inline constexpr std::string_view kAncientFogletDeployEffect = "supported.ancient_foglet.deploy";
inline constexpr std::string_view kCaranthirGoldenChildTurnStartEffect = "supported.caranthir_golden_child.turn_start";
inline constexpr std::string_view kTirNaLiaDeployEffect = "supported.tir_na_lia.deploy";
inline constexpr std::string_view kTirNaLiaOrderEffect = "supported.tir_na_lia.order";
inline constexpr std::string_view kTirNaLiaRoundEndEffect = "supported.tir_na_lia.round_end";
inline constexpr std::string_view kArdGaethSpecialEffect = "supported.ard_gaeth.special";

CardDefinition make_blood_scent_definition();
CardDefinition make_white_frost_definition();
CardDefinition make_ekimmara_definition();
CardDefinition make_armorer_workshop_definition();
CardDefinition make_crystal_skull_definition();
CardDefinition make_regis_reborn_definition();
CardDefinition make_unseen_elder_definition();
CardDefinition make_giant_centipede_definition();
CardDefinition make_dettlaff_aep_definition();
CardDefinition make_lord_riptide_definition();
CardDefinition make_katakan_definition();
CardDefinition make_verena_definition();
CardDefinition make_naglfar_definition();
CardDefinition make_imlerith_definition();
CardDefinition make_geels_definition();
CardDefinition make_ozzrel_definition();
CardDefinition make_queen_of_the_night_definition();
CardDefinition make_fleder_definition();
CardDefinition make_garkain_definition();
CardDefinition make_bruxa_definition();
CardDefinition make_feast_of_blood_definition();
CardDefinition make_aen_elle_conqueror_definition();
CardDefinition make_bloodscented_predator_definition();
CardDefinition make_wild_hunt_navigator_definition();
CardDefinition make_wild_hunt_rider_definition();
CardDefinition make_wild_hunt_warrior_definition();
CardDefinition make_wild_hunt_hound_definition();
CardDefinition make_wild_hunt_bruiser_definition();
CardDefinition make_naglfar_crew_definition();
CardDefinition make_naglfar_taskmaster_definition();
CardDefinition make_aen_elle_aristocrat_definition();
CardDefinition make_aen_elle_slave_trader_definition();
CardDefinition make_oberon_king_definition();
CardDefinition make_oberon_invader_definition();
CardDefinition make_oberon_conqueror_definition();
CardDefinition make_imleriths_wrath_definition();
CardDefinition make_apiarian_phantom_definition();
CardDefinition make_eredin_breacc_glas_definition();
CardDefinition make_winter_queen_definition();
CardDefinition make_red_riders_definition();
CardDefinition make_ancient_foglet_definition();
CardDefinition make_caranthir_golden_child_definition();
CardDefinition make_tir_na_lia_definition();
CardDefinition make_ard_gaeth_definition();

[[nodiscard]] std::vector<CardDefinition> make_all_definitions();
[[nodiscard]] CardCatalog make_catalog();
// Registers deterministic effects that the current C++ kernel can
// express. Cards whose full Python behavior still needs future engine features
// are present in the catalog as data-only definitions.
void register_basic_supported_card_effects(EffectRegistry& registry);
void register_supported_card_effects(EffectRegistry& registry);

}  // namespace supported_cards
}  // namespace gwent

