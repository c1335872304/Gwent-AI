#include "gwent/generated/supported_card_data.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace gwent {
namespace generated {
namespace {

CardDefinition make_card_202185() {
    CardDefinition definition = make_leader_definition("202185", "腥膻之味", Faction::Monsters);
    definition.provision = 17;
    definition.color = "Leader";
    definition.rarity = "Legendary";
    definition.description = "指令：使 1 个敌军单位重伤 3 回合。充能：3。所有充能均已使用后，在己方单排生成 1 只蝙魔。";
    definition.effect_ids = {"leader:supported.blood_scent.leader"};
    definition.categories = {"领袖牌"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("charges", "3");
    definition.metadata.emplace("leader_target_selector", "enemy_unit");
    definition.metadata.emplace("order", "true");
    definition.metadata.emplace("provision_bonus", "17");
    definition.metadata.emplace("requires_target", "enemy_unit");
    return definition;
}

CardDefinition make_card_202497() {
    CardDefinition definition = make_stratagem_definition("202497", "注魔盔甲");
    definition.color = "Gold";
    definition.rarity = "Legendary";
    definition.description = "指令：使手牌中 1 张单位牌获得 3 点增益和 2 点护甲。";
    definition.effect_ids = {"order:supported.armorer_workshop.order"};
    definition.metadata.emplace("order", "true");
    definition.metadata.emplace("order_target_selector", "own_hand_unit");
    definition.metadata.emplace("zeal", "true");
    return definition;
}

CardDefinition make_card_203099() {
    CardDefinition definition = make_unit_definition("203099", "雷吉斯：重生", 1, Faction::Monsters);
    definition.provision = 12;
    definition.color = "Gold";
    definition.rarity = "Legendary";
    definition.description = "部署：汲食 1 个敌军单位 3 点。己方回合结束时，若位于手牌或牌组中，且有敌军单位重伤，则自身基础战力提高 1 点。";
    definition.effect_ids = {"deploy:supported.regis.deploy", "turn_end:supported.regis.turn_end"};
    definition.categories = {"吸血鬼"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("implementation", "partial");
    definition.metadata.emplace("python_effect", "deck_a.regis_reborn");
    definition.metadata.emplace("turn_end_active_zones", "hand,deck");
    return definition;
}

CardDefinition make_card_202889() {
    CardDefinition definition = make_unit_definition("202889", "暗影长者", 7, Faction::Monsters);
    definition.provision = 11;
    definition.color = "Gold";
    definition.rarity = "Legendary";
    definition.description = "部署：使 1 个敌军单位重伤 4 回合。己方回合结束时，随机使 1 名没有重伤的敌军单位重伤 2 回合。赤诚：己方回合结束时，还会对所有敌军单位触发重伤效果。";
    definition.effect_ids = {"deploy:supported.unseen_elder.deploy", "turn_end:supported.unseen_elder.turn_end"};
    definition.categories = {"吸血鬼"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("implementation", "implemented");
    definition.metadata.emplace("python_effect", "deck_a.unseen_elder");
    return definition;
}

CardDefinition make_card_202437() {
    CardDefinition definition = make_unit_definition("202437", "巨型蜈蚣", 16, Faction::Monsters);
    definition.provision = 10;
    definition.base_armor = 0;
    definition.color = "Gold";
    definition.rarity = "Legendary";
    definition.description = "部署：获得等同于手牌数量的护甲。若该单位没有护甲，或护甲被击破，则摧毁自身。";
    definition.effect_ids = {"deploy:supported.giant_centipede.deploy", "armor_broken:supported.giant_centipede.armor_broken"};
    definition.categories = {"类虫生物"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("implementation", "implemented");
    definition.metadata.emplace("python_effect", "deck_a.giant_centipede");
    return definition;
}

CardDefinition make_card_202888() {
    CardDefinition definition = make_unit_definition("202888", "狄拉夫·艾瑞廷", 7, Faction::Monsters);
    definition.provision = 10;
    definition.color = "Gold";
    definition.rarity = "Legendary";
    definition.description = "部署：在对方单排生成血月。指令：对 1 名带有重伤的敌军单位造成 1 点伤害。致死：在同排生成 1 只蝙魔。冷却：1。";
    definition.effect_ids = {"deploy:supported.dettlaff.deploy", "order:supported.dettlaff.order", "card_destroyed:supported.dettlaff.deathblow"};
    definition.categories = {"吸血鬼"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("cooldown", "1");
    definition.metadata.emplace("implementation", "implemented");
    definition.metadata.emplace("order", "true");
    definition.metadata.emplace("order_target_selector", "bleeding_enemy_unit");
    definition.metadata.emplace("python_effect", "deck_a.dettlaff_aep");
    definition.metadata.emplace("zeal", "true");
    return definition;
}

CardDefinition make_card_203265() {
    CardDefinition definition = make_unit_definition("203265", "裂流之主", 9, Faction::Monsters);
    definition.provision = 9;
    definition.base_armor = 2;
    definition.color = "Gold";
    definition.rarity = "Epic";
    definition.description = "部署（近战）：与战力最高的敌军单位交锋。威势：己方回合结束时，若位于手牌中，则获得 1 点护甲。";
    definition.effect_ids = {"deploy:supported.lord_riptide.deploy", "turn_end:supported.lord_riptide.hand_end"};
    definition.categories = {"食人魔", "战士"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("deploy_rows", "melee");
    definition.metadata.emplace("implementation", "implemented");
    definition.metadata.emplace("python_effect", "deck_a.lord_riptide");
    return definition;
}

CardDefinition make_card_132220() {
    CardDefinition definition = make_unit_definition("132220", "卡塔卡恩", 7, Faction::Monsters);
    definition.provision = 9;
    definition.color = "Gold";
    definition.rarity = "Epic";
    definition.description = "狂热、指令：生成 1 只蝙魔至同排。冷却：4。每有 1 个敌军单位遭受重伤，则冷却 -1。";
    definition.effect_ids = {"deploy:supported.katakan.deploy", "order:supported.katakan.order"};
    definition.categories = {"吸血鬼"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("cooldown", "4");
    definition.metadata.emplace("order", "true");
    definition.metadata.emplace("zeal", "true");
    return definition;
}

CardDefinition make_card_203284() {
    CardDefinition definition = make_unit_definition("203284", "薇瑞娜", 6, Faction::Monsters);
    definition.provision = 9;
    definition.color = "Gold";
    definition.rarity = "Legendary";
    definition.description = "部署：使 1 个敌军单位重伤 4 回合。重伤的敌军单位无法获得增益。";
    definition.effect_ids = {"deploy:supported.verena.deploy", "status_added:supported.verena.bleeding_added"};
    definition.categories = {"吸血鬼"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("implementation", "implemented");
    definition.metadata.emplace("python_effect", "deck_a.verena");
    return definition;
}

CardDefinition make_card_200301() {
    CardDefinition definition = make_special_definition("200301", "纳吉尔法", Faction::Monsters);
    definition.provision = 8;
    definition.color = "Gold";
    definition.rarity = "Legendary";
    definition.description = "检视己方牌组 2 张随机金色牌，随后打出 1 张，将其余卡牌置于牌组顶部。";
    definition.effect_ids = {"special:supported.naglfar.special"};
    definition.categories = {"狂猎"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("implementation", "implemented");
    definition.metadata.emplace("python_effect", "deck_a.naglfar");
    return definition;
}

CardDefinition make_card_201781() {
    CardDefinition definition = make_unit_definition("201781", "伊勒瑞斯", 3, Faction::Monsters);
    definition.provision = 8;
    definition.color = "Gold";
    definition.rarity = "Legendary";
    definition.description = "部署（近战）：抽 1 张牌，随后丢弃 1 张牌。若丢弃掉的牌为单位牌，则自身获得其战力的增益。";
    definition.effect_ids = {"deploy:supported.imlerith.deploy"};
    definition.categories = {"精灵", "狂猎", "战士"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("deploy_rows", "melee");
    definition.metadata.emplace("implementation", "implemented");
    definition.metadata.emplace("python_effect", "deck_a.imlerith");
    return definition;
}

CardDefinition make_card_131102() {
    CardDefinition definition = make_unit_definition("131102", "盖尔", 2, Faction::Monsters);
    definition.provision = 8;
    definition.color = "Gold";
    definition.rarity = "Legendary";
    definition.description = "部署：从牌组中打出 1 张狂猎特殊牌。赤诚：改为从牌组中打出 1 张狂猎牌。";
    definition.effect_ids = {"deploy:supported.geels.deploy"};
    definition.categories = {"精灵", "狂猎"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("implementation", "implemented");
    definition.metadata.emplace("python_effect", "deck_a.geels");
    return definition;
}

CardDefinition make_card_201698() {
    CardDefinition definition = make_unit_definition("201698", "欧兹瑞尔", 1, Faction::Monsters);
    definition.provision = 8;
    definition.color = "Gold";
    definition.rarity = "Epic";
    definition.description = "部署（近战）：吞噬对方墓场中的 1 个单位。部署（远程）：吞噬己方墓场中的 1 个单位。";
    definition.effect_ids = {"deploy:supported.ozzrel.deploy"};
    definition.categories = {"食腐生物"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("implementation", "implemented");
    definition.metadata.emplace("python_effect", "deck_a.ozzrel");
    return definition;
}

CardDefinition make_card_202221() {
    CardDefinition definition = make_unit_definition("202221", "夜之女王", 6, Faction::Monsters);
    definition.provision = 6;
    definition.color = "Gold";
    definition.rarity = "Epic";
    definition.description = "部署（近战）：使 1 名敌军单位重伤 3 回合。部署（远程）：净化 1 个单位。";
    definition.effect_ids = {"deploy:supported.queen_of_the_night.deploy"};
    definition.categories = {"吸血鬼"};
    definition.main_category = definition.categories.front();
    return definition;
}

CardDefinition make_card_202229() {
    CardDefinition definition = make_unit_definition("202229", "蝠翼魔", 4, Faction::Monsters);
    definition.provision = 6;
    definition.color = "Bronze";
    definition.rarity = "Rare";
    definition.description = "当敌军单位遭受重伤，自身获得所施加重伤回合数的增益。倒数：1。己方回合开始时倒数刷新。";
    definition.effect_ids = {"deploy:supported.fleder.deploy"};
    definition.categories = {"吸血鬼"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("countdown", "1");
    return definition;
}

CardDefinition make_card_202233() {
    CardDefinition definition = make_unit_definition("202233", "夜行吸血鬼", 5, Faction::Monsters);
    definition.provision = 5;
    definition.color = "Bronze";
    definition.rarity = "Rare";
    definition.description = "狂热。指令（近战）：使 1 个敌军单位重伤 2 回合。冷却：2。己方每打出 1 张吸血鬼牌，则冷却 -1。";
    definition.effect_ids = {"deploy:supported.garkain.deploy", "order:supported.garkain.order"};
    definition.categories = {"吸血鬼"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("cooldown", "2");
    definition.metadata.emplace("order", "true");
    definition.metadata.emplace("order_rows", "melee");
    definition.metadata.emplace("order_target_selector", "enemy_unit");
    definition.metadata.emplace("zeal", "true");
    return definition;
}

CardDefinition make_card_202230() {
    CardDefinition definition = make_unit_definition("202230", "吸血鬼女", 4, Faction::Monsters);
    definition.provision = 5;
    definition.color = "Bronze";
    definition.rarity = "Common";
    definition.description = "部署：使 1 名敌军单位重伤 3 回合。指令：使 1 名敌军单位重伤 3 回合。";
    definition.effect_ids = {"deploy:supported.bruxa.deploy", "order:supported.bruxa.order"};
    definition.categories = {"吸血鬼"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("order", "true");
    definition.metadata.emplace("order_target_selector", "enemy_unit");
    return definition;
}

CardDefinition make_card_202228() {
    CardDefinition definition = make_special_definition("202228", "猩红盛宴", Faction::Monsters);
    definition.provision = 4;
    definition.color = "Bronze";
    definition.rarity = "Common";
    definition.description = "净化 1 个敌军单位，并对其造成 3 点伤害。若己方控制 1 个吸血鬼，还将使其重伤 3 回合。";
    definition.effect_ids = {"special:supported.feast_of_blood.special"};
    definition.categories = {"生物"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("implementation", "implemented");
    definition.metadata.emplace("python_effect", "deck_a.feast_of_blood");
    return definition;
}

CardDefinition make_card_202614() {
    CardDefinition definition = make_unit_definition("202614", "艾恩·艾尔征服者", 8, Faction::Monsters);
    definition.provision = 4;
    definition.color = "Bronze";
    definition.rarity = "Common";
    definition.description = "遮蔽。部署：摧毁自身。赤诚：取消部署能力。";
    definition.effect_ids = {"deploy:supported.aen_elle_conqueror.deploy"};
    definition.categories = {"精灵", "狂猎"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("veil", "true");
    return definition;
}

CardDefinition make_card_202231() {
    CardDefinition definition = make_unit_definition("202231", "渴血鸟怪", 5, Faction::Monsters);
    definition.provision = 4;
    definition.color = "Bronze";
    definition.rarity = "Common";
    definition.description = "部署：使 1 个敌军单位重伤 2 回合。会师：改为使 1 个敌军单位重伤，回合数等同于其基础战力。";
    definition.effect_ids = {"deploy:supported.bloodscented_predator.deploy"};
    definition.categories = {"吸血鬼"};
    definition.main_category = definition.categories.front();
    return definition;
}

CardDefinition make_card_200026() {
    CardDefinition definition = make_unit_definition("200026", "狂猎导航员", 3, Faction::Monsters);
    definition.provision = 4;
    definition.color = "Bronze";
    definition.rarity = "Common";
    definition.description = "部署：使 1 个友军单位获得增益，增益值等同于其所在排对应的敌方排上霜的持续回合数。统御：改为等同于敌方半场霜的总持续回合数。";
    definition.effect_ids = {"deploy:supported.wild_hunt_navigator.deploy"};
    definition.categories = {"精灵", "狂猎", "法师"};
    definition.main_category = definition.categories.front();
    return definition;
}

CardDefinition make_card_132309() {
    CardDefinition definition = make_unit_definition("132309", "狂猎战士", 3, Faction::Monsters);
    definition.provision = 4;
    definition.color = "Bronze";
    definition.rarity = "Common";
    definition.description = "部署：对 1 个敌军单位造成 2 点伤害。统御：还会在此单位同排生成霜，持续 1 回合。";
    definition.effect_ids = {"deploy:supported.wild_hunt_warrior.deploy"};
    definition.categories = {"精灵", "狂猎", "战士"};
    definition.main_category = definition.categories.front();
    return definition;
}

CardDefinition make_card_132310() {
    CardDefinition definition = make_unit_definition("132310", "狂猎骑士", 4, Faction::Monsters);
    definition.provision = 4;
    definition.color = "Bronze";
    definition.rarity = "Rare";
    definition.description = "部署、统御：从己方牌组召唤所有自身的同名牌至同排。";
    definition.effect_ids = {"deploy:supported.wild_hunt_rider.deploy"};
    definition.categories = {"精灵", "狂猎", "战士"};
    definition.main_category = definition.categories.front();
    return definition;
}

CardDefinition make_card_132402() {
    CardDefinition definition = make_unit_definition("132402", "狂猎之犬", 4, Faction::Monsters);
    definition.provision = 4;
    definition.color = "Bronze";
    definition.rarity = "Common";
    definition.description = "统御：己方回合结束时，自身获得 1 点增益。";
    definition.effect_ids = {"turn_end:supported.wild_hunt_hound.turn_end"};
    definition.categories = {"野兽", "狂猎"};
    definition.main_category = definition.categories.front();
    return definition;
}

CardDefinition make_card_202611() {
    CardDefinition definition = make_unit_definition("202611", "狂猎碾压者", 5, Faction::Monsters);
    definition.provision = 5;
    definition.base_armor = 1;
    definition.color = "Bronze";
    definition.rarity = "Rare";
    definition.description = "部署：将 1 个敌军单位移至所在半场的另一排。若所移至排上有霜，则对其造成 2 点伤害。";
    definition.effect_ids = {"deploy:supported.wild_hunt_bruiser.deploy"};
    definition.categories = {"精灵", "狂猎"};
    definition.main_category = definition.categories.front();
    return definition;
}

CardDefinition make_card_202612() {
    CardDefinition definition = make_unit_definition("202612", "纳吉尔法船员", 1, Faction::Monsters);
    definition.provision = 5;
    definition.base_armor = 1;
    definition.color = "Bronze";
    definition.rarity = "Rare";
    definition.description = "部署：生成霜至敌方单排，持续 2 回合。己方回合结束时，若敌方同排上有霜，则自身获得 1 点增益。";
    definition.effect_ids = {"deploy:supported.naglfar_crew.deploy", "turn_end:supported.naglfar_crew.turn_end"};
    definition.categories = {"精灵", "狂猎"};
    definition.main_category = definition.categories.front();
    return definition;
}

CardDefinition make_card_202613() {
    CardDefinition definition = make_unit_definition("202613", "纳吉尔法工头", 5, Faction::Monsters);
    definition.provision = 4;
    definition.color = "Bronze";
    definition.rarity = "Common";
    definition.description = "部署：净化 1 个敌军单位。统御：改为净化 1 个单位。";
    definition.effect_ids = {"deploy:supported.naglfar_taskmaster.deploy"};
    definition.categories = {"精灵", "狂猎"};
    definition.main_category = definition.categories.front();
    return definition;
}

CardDefinition make_card_203160() {
    CardDefinition definition = make_unit_definition("203160", "艾恩·艾尔贵族", 4, Faction::Monsters);
    definition.provision = 5;
    definition.color = "Bronze";
    definition.rarity = "Rare";
    definition.description = "指令、统御：将 1 个敌军单位移至对方另一排。己方回合结束时，若对方任意一排在本回合内被施加过霜，则生成霜至该排，持续 1 回合。";
    definition.effect_ids = {"order:supported.aen_elle_aristocrat.order", "turn_end:supported.aen_elle_aristocrat.turn_end"};
    definition.categories = {"精灵", "狂猎", "望族"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("order", "true");
    definition.metadata.emplace("order_charges", "1");
    definition.metadata.emplace("order_requires_dominance", "true");
    return definition;
}

CardDefinition make_card_203161() {
    CardDefinition definition = make_unit_definition("203161", "艾恩·艾尔奴隶商", 4, Faction::Monsters);
    definition.provision = 5;
    definition.color = "Bronze";
    definition.rarity = "Common";
    definition.description = "部署：获得活力，数值等同于敌方半场霜的总持续回合数，并灌注 1 个敌军单位：其己方回合结束时，若存在战力高于自身的敌方艾恩·艾尔奴隶商，则将自身战力设为 1，随后锁定自身。";
    definition.effect_ids = {"deploy:supported.aen_elle_slave_trader.deploy"};
    definition.categories = {"精灵", "狂猎"};
    definition.main_category = definition.categories.front();
    return definition;
}

CardDefinition make_card_202603() {
    CardDefinition definition = make_unit_definition("202603", "奥贝伦王", 6, Faction::Monsters);
    definition.provision = 12;
    definition.color = "Gold";
    definition.rarity = "Legendary";
    definition.description = "部署：生成并随机打出1张铜色狂猎单位牌。小局开始时，若位于手牌或牌组，发生历变。";
    definition.effect_ids = {"deploy:supported.oberon_king.deploy", "round_start:supported.oberon_king.round_start"};
    definition.categories = {"精灵", "狂猎"};
    definition.main_category = definition.categories.front();
    return definition;
}

CardDefinition make_card_202604() {
    CardDefinition definition = make_unit_definition("202604", "奥贝伦王：入侵者", 6, Faction::Monsters);
    definition.provision = 12;
    definition.color = "Gold";
    definition.rarity = "Legendary";
    definition.description = "部署：创造并打出1张铜色狂猎单位牌。赤诚：小局开始时，若位于手牌或牌组，发生历变。";
    definition.effect_ids = {"deploy:supported.oberon_invader.deploy", "round_start:supported.oberon_invader.round_start"};
    definition.categories = {"精灵", "狂猎"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("derived_evolution_form", "true");
    return definition;
}

CardDefinition make_card_202605() {
    CardDefinition definition = make_unit_definition("202605", "奥贝伦王：征服者", 6, Faction::Monsters);
    definition.provision = 12;
    definition.color = "Gold";
    definition.rarity = "Legendary";
    definition.description = "遮蔽。部署：从初始牌组生成并打出1张铜色狂猎单位牌。每当己方打出狂猎单位，使其获得1点增益。";
    definition.effect_ids = {"deploy:supported.oberon_conqueror.deploy", "card_played:supported.oberon_conqueror.card_played"};
    definition.categories = {"精灵", "狂猎"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("derived_evolution_form", "true");
    definition.metadata.emplace("veil", "true");
    return definition;
}

CardDefinition make_card_200055() {
    CardDefinition definition = make_leader_definition("200055", "白霜降临", Faction::Monsters);
    definition.provision = 15;
    definition.color = "Leader";
    definition.rarity = "Legendary";
    definition.description = "指令：将1个敌军单位移至另一排，并在目标排生成霜2回合。充能：2。己方打出狂猎单位时，若敌方对应排有霜，使其增益1。";
    definition.effect_ids = {"leader:supported.white_frost.leader", "card_played:supported.white_frost.card_played"};
    definition.categories = {"领袖牌"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("charges", "2");
    definition.metadata.emplace("leader", "true");
    definition.metadata.emplace("leader_target_selector", "enemy_unit");
    definition.metadata.emplace("order", "true");
    definition.metadata.emplace("provision_bonus", "15");
    definition.metadata.emplace("requires_target", "enemy_unit");
    return definition;
}

CardDefinition make_card_202493() {
    CardDefinition definition = make_stratagem_definition("202493", "水晶面甲");
    definition.provision = 0;
    definition.color = "Gold";
    definition.rarity = "Legendary";
    definition.description = "指令：使1个友军单位获得4点增益和遮蔽。";
    definition.effect_ids = {"order:supported.crystal_skull.order"};
    definition.metadata.emplace("order", "true");
    definition.metadata.emplace("order_charges", "1");
    definition.metadata.emplace("order_target_selector", "allied_unit");
    definition.metadata.emplace("stratagem", "true");
    definition.metadata.emplace("zeal", "true");
    return definition;
}

CardDefinition make_card_132102() {
    CardDefinition definition = make_special_definition("132102", "伊勒瑞斯之怒", Faction::Monsters);
    definition.provision = 8;
    definition.color = "Gold";
    definition.rarity = "Legendary";
    definition.description = "对1个敌军单位造成等同于己方最强单位战力的伤害；若目标排有霜则改为摧毁。";
    definition.effect_ids = {"special:supported.imleriths_wrath.special"};
    definition.categories = {"狂猎"};
    definition.main_category = definition.categories.front();
    return definition;
}

CardDefinition make_card_202609() {
    CardDefinition definition = make_unit_definition("202609", "蜜蜂幽灵", 4, Faction::Monsters);
    definition.provision = 7;
    definition.base_armor = 1;
    definition.color = "Gold";
    definition.rarity = "Epic";
    definition.description = "遮蔽。狂热。指令（近战）：对1个敌军单位造成3点伤害。己方回合结束时，若指令尚未使用，自身增益1。";
    definition.effect_ids = {"order:supported.apiarian_phantom.order", "turn_end:supported.apiarian_phantom.turn_end"};
    definition.categories = {"野兽", "狂猎"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("order", "true");
    definition.metadata.emplace("order_charges", "1");
    definition.metadata.emplace("order_rows", "melee");
    definition.metadata.emplace("order_target_selector", "enemy_unit");
    definition.metadata.emplace("veil", "true");
    definition.metadata.emplace("zeal", "true");
    return definition;
}

CardDefinition make_card_202606() {
    CardDefinition definition = make_unit_definition("202606", "艾瑞汀‧布里克‧葛拉斯", 7, Faction::Monsters);
    definition.provision = 9;
    definition.color = "Gold";
    definition.rarity = "Legendary";
    definition.description = "部署：生成霜至对方单排，持续2回合。统御：霜造成的伤害提高1点。";
    definition.effect_ids = {"deploy:supported.eredin.deploy"};
    definition.categories = {"精灵", "狂猎"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("frost_damage_bonus_dominance", "1");
    return definition;
}

CardDefinition make_card_202608() {
    CardDefinition definition = make_unit_definition("202608", "冬之女王", 3, Faction::Monsters);
    definition.provision = 8;
    definition.color = "Gold";
    definition.rarity = "Epic";
    definition.description = "己方回合结束时，若对方两排均有霜，则从牌组召唤至远程排。赤诚：双方放弃跟牌后，增益等同于对方霜剩余回合数的2倍。";
    definition.effect_ids = {"turn_end:supported.winter_queen.turn_end", "round_end:supported.winter_queen.round_end"};
    definition.categories = {"精灵", "狂猎"};
    definition.main_category = definition.categories.front();
    return definition;
}

CardDefinition make_card_202610() {
    CardDefinition definition = make_special_definition("202610", "红骑士", Faction::Monsters);
    definition.provision = 5;
    definition.color = "Bronze";
    definition.rarity = "Rare";
    definition.description = "择一：单排霜4；单排霜2并重新打出1个铜色狂猎单位；或两排各霜2。";
    definition.effect_ids = {"special:supported.red_riders.special"};
    definition.categories = {"狂猎"};
    definition.main_category = definition.categories.front();
    return definition;
}

CardDefinition make_card_202703() {
    CardDefinition definition = make_special_definition("202703", "红骑士：严霜", Faction::Monsters);
    definition.provision = 0;
    definition.doomed = true;
    definition.is_token = true;
    definition.color = "Token";
    definition.rarity = "Token";
    definition.description = "在对方单排生成霜4回合。";
    definition.metadata.emplace("derived_choice_form", "true");
    return definition;
}

CardDefinition make_card_202704() {
    CardDefinition definition = make_special_definition("202704", "红骑士：重整", Faction::Monsters);
    definition.provision = 0;
    definition.doomed = true;
    definition.is_token = true;
    definition.color = "Token";
    definition.rarity = "Token";
    definition.description = "在对方单排生成霜2回合，随后重新打出铜色狂猎单位。";
    definition.metadata.emplace("derived_choice_form", "true");
    return definition;
}

CardDefinition make_card_203125() {
    CardDefinition definition = make_special_definition("203125", "红骑士：双霜", Faction::Monsters);
    definition.provision = 0;
    definition.doomed = true;
    definition.is_token = true;
    definition.color = "Token";
    definition.rarity = "Token";
    definition.description = "在对方两排各生成霜2回合。";
    definition.metadata.emplace("derived_choice_form", "true");
    return definition;
}

CardDefinition make_card_132302() {
    CardDefinition definition = make_unit_definition("132302", "远古小雾妖", 4, Faction::Monsters);
    definition.provision = 6;
    definition.color = "Bronze";
    definition.rarity = "Rare";
    definition.description = "遮蔽。部署：增益等同于敌方所有整排效果的总剩余回合数。己方每施加整排效果时，增益相同回合数。";
    definition.effect_ids = {"deploy:supported.ancient_foglet.deploy"};
    definition.categories = {"食腐生物"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("boost_on_row_effect_applied", "true");
    definition.metadata.emplace("veil", "true");
    return definition;
}

CardDefinition make_card_203159() {
    CardDefinition definition = make_unit_definition("203159", "卡兰希尔：金童", 1, Faction::Monsters);
    definition.provision = 9;
    definition.color = "Gold";
    definition.rarity = "Epic";
    definition.description = "己方回合开始时，若位于手牌或牌组中，重置自身战力并获得等同于之前5个敌方回合中霜实际造成战力伤害量的增益。";
    definition.effect_ids = {"turn_start:supported.caranthir_golden_child.turn_start"};
    definition.categories = {"精灵", "狂猎", "法师"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("tracks_frost_damage_history", "true");
    return definition;
}

CardDefinition make_card_203158() {
    CardDefinition definition = make_artifact_definition("203158", "提尔纳丽雅", Faction::Monsters);
    definition.provision = 12;
    definition.color = "Gold";
    definition.rarity = "Legendary";
    definition.description = "坚韧。部署：按初始牌组中不同铜色狂猎牌数量增益一个友军单位；无其他友军时跳过。指令：生成并打出红骑士。赤诚：使用指令时，先在敌方两排恢复上一小局转换时失去的剩余霜。";
    definition.effect_ids = {"deploy:supported.tir_na_lia.deploy", "order:supported.tir_na_lia.order", "round_end:supported.tir_na_lia.round_end"};
    definition.categories = {"地点", "狂猎"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("deploy_target_selector", "allied_unit_optional");
    definition.metadata.emplace("order", "true");
    definition.metadata.emplace("order_charges", "1");
    definition.metadata.emplace("resilience", "true");
    definition.metadata.emplace("saved_frost_memory", "true");
    return definition;
}

CardDefinition make_card_202607() {
    CardDefinition definition = make_special_definition("202607", "诸界之门", Faction::Monsters);
    definition.provision = 8;
    definition.color = "Gold";
    definition.rarity = "Legendary";
    definition.description = "回响。在对方近战排和远程排各生成霜，持续3回合。";
    definition.effect_ids = {"special:supported.ard_gaeth.special"};
    definition.categories = {"狂猎"};
    definition.main_category = definition.categories.front();
    definition.metadata.emplace("echo", "true");
    return definition;
}

CardDefinition make_card_132313() {
    CardDefinition definition = make_unit_definition("132313", "蝙魔", 3, Faction::Monsters);
    definition.provision = 0;
    definition.doomed = true;
    definition.is_token = true;
    definition.color = "Token";
    definition.rarity = "Token";
    definition.categories = {"吸血鬼"};
    definition.main_category = definition.categories.front();
    return definition;
}

}  // namespace

std::string_view supported_card_data_schema_version() noexcept {
    return "gwent-card-data-v1";
}

std::string_view supported_card_data_source_path() noexcept {
    return "data/cards/supported_cards.json";
}

std::string_view deck_a_deck_data_source_path() noexcept {
    return "data/decks/deck_a.json";
}

std::string_view deck_b_deck_data_source_path() noexcept {
    return "data/decks/deck_b.json";
}

std::vector<std::string_view> supported_deck_ids() {
    return {
        "deck_a",
        "deck_b",
    };
}

DeckSpec make_supported_deck_spec(std::string_view id) {
    if (id == "deck_a") {
        return make_deck_a_deck_spec();
    }
    if (id == "deck_b") {
        return make_deck_b_deck_spec();
    }
    throw std::out_of_range("unknown supported deck id: " + std::string(id));
}

CardDefinition make_supported_card_definition(std::string_view id) {
    if (id == "202185") {
        return make_card_202185();
    }
    if (id == "202497") {
        return make_card_202497();
    }
    if (id == "203099") {
        return make_card_203099();
    }
    if (id == "202889") {
        return make_card_202889();
    }
    if (id == "202437") {
        return make_card_202437();
    }
    if (id == "202888") {
        return make_card_202888();
    }
    if (id == "203265") {
        return make_card_203265();
    }
    if (id == "132220") {
        return make_card_132220();
    }
    if (id == "203284") {
        return make_card_203284();
    }
    if (id == "200301") {
        return make_card_200301();
    }
    if (id == "201781") {
        return make_card_201781();
    }
    if (id == "131102") {
        return make_card_131102();
    }
    if (id == "201698") {
        return make_card_201698();
    }
    if (id == "202221") {
        return make_card_202221();
    }
    if (id == "202229") {
        return make_card_202229();
    }
    if (id == "202233") {
        return make_card_202233();
    }
    if (id == "202230") {
        return make_card_202230();
    }
    if (id == "202228") {
        return make_card_202228();
    }
    if (id == "202614") {
        return make_card_202614();
    }
    if (id == "202231") {
        return make_card_202231();
    }
    if (id == "200026") {
        return make_card_200026();
    }
    if (id == "132309") {
        return make_card_132309();
    }
    if (id == "132310") {
        return make_card_132310();
    }
    if (id == "132402") {
        return make_card_132402();
    }
    if (id == "202611") {
        return make_card_202611();
    }
    if (id == "202612") {
        return make_card_202612();
    }
    if (id == "202613") {
        return make_card_202613();
    }
    if (id == "203160") {
        return make_card_203160();
    }
    if (id == "203161") {
        return make_card_203161();
    }
    if (id == "202603") {
        return make_card_202603();
    }
    if (id == "202604") {
        return make_card_202604();
    }
    if (id == "202605") {
        return make_card_202605();
    }
    if (id == "200055") {
        return make_card_200055();
    }
    if (id == "202493") {
        return make_card_202493();
    }
    if (id == "132102") {
        return make_card_132102();
    }
    if (id == "202609") {
        return make_card_202609();
    }
    if (id == "202606") {
        return make_card_202606();
    }
    if (id == "202608") {
        return make_card_202608();
    }
    if (id == "202610") {
        return make_card_202610();
    }
    if (id == "202703") {
        return make_card_202703();
    }
    if (id == "202704") {
        return make_card_202704();
    }
    if (id == "203125") {
        return make_card_203125();
    }
    if (id == "132302") {
        return make_card_132302();
    }
    if (id == "203159") {
        return make_card_203159();
    }
    if (id == "203158") {
        return make_card_203158();
    }
    if (id == "202607") {
        return make_card_202607();
    }
    if (id == "132313") {
        return make_card_132313();
    }
    throw std::out_of_range("unknown supported card id: " + std::string(id));
}

std::vector<CardDefinition> make_supported_card_definitions() {
    return {
        make_card_202185(),
        make_card_202497(),
        make_card_203099(),
        make_card_202889(),
        make_card_202437(),
        make_card_202888(),
        make_card_203265(),
        make_card_132220(),
        make_card_203284(),
        make_card_200301(),
        make_card_201781(),
        make_card_131102(),
        make_card_201698(),
        make_card_202221(),
        make_card_202229(),
        make_card_202233(),
        make_card_202230(),
        make_card_202228(),
        make_card_202614(),
        make_card_202231(),
        make_card_200026(),
        make_card_132309(),
        make_card_132310(),
        make_card_132402(),
        make_card_202611(),
        make_card_202612(),
        make_card_202613(),
        make_card_203160(),
        make_card_203161(),
        make_card_202603(),
        make_card_202604(),
        make_card_202605(),
        make_card_200055(),
        make_card_202493(),
        make_card_132102(),
        make_card_202609(),
        make_card_202606(),
        make_card_202608(),
        make_card_202610(),
        make_card_202703(),
        make_card_202704(),
        make_card_203125(),
        make_card_132302(),
        make_card_203159(),
        make_card_203158(),
        make_card_202607(),
        make_card_132313(),
    };
}

DeckSpec make_deck_a_deck_spec() {
    DeckSpec deck;
    deck.leader = make_card_202185();
    deck.stratagem = make_card_202497();
    deck.cards = {
        {make_card_203099(), 1},
        {make_card_202889(), 1},
        {make_card_202437(), 1},
        {make_card_202888(), 1},
        {make_card_203265(), 1},
        {make_card_132220(), 1},
        {make_card_203284(), 1},
        {make_card_200301(), 1},
        {make_card_201781(), 1},
        {make_card_131102(), 1},
        {make_card_201698(), 1},
        {make_card_202221(), 1},
        {make_card_202229(), 2},
        {make_card_202233(), 2},
        {make_card_202230(), 2},
        {make_card_202228(), 1},
        {make_card_202614(), 2},
        {make_card_202231(), 2},
        {make_card_132310(), 2},
    };
    return deck;
}

DeckSpec make_deck_b_deck_spec() {
    DeckSpec deck;
    deck.leader = make_card_200055();
    deck.stratagem = make_card_202493();
    deck.cards = {
        {make_card_203158(), 1},
        {make_card_202603(), 1},
        {make_card_203265(), 1},
        {make_card_202606(), 1},
        {make_card_203159(), 1},
        {make_card_132102(), 1},
        {make_card_200301(), 1},
        {make_card_202607(), 1},
        {make_card_202608(), 1},
        {make_card_131102(), 1},
        {make_card_202609(), 1},
        {make_card_132302(), 2},
        {make_card_202610(), 2},
        {make_card_202611(), 1},
        {make_card_203160(), 2},
        {make_card_202612(), 2},
        {make_card_202614(), 1},
        {make_card_202613(), 1},
        {make_card_132309(), 1},
        {make_card_200026(), 2},
    };
    return deck;
}


}  // namespace generated
}  // namespace gwent
