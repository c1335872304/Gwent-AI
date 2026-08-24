import type { GameAction, GameObject } from "../types/game"

const ACTION_NAMES: Record<string, string> = {
  unknown: "未知操作",
  pass: "放弃跟牌",
  end_turn: "结束回合",
  play_card: "打出",
  mulligan: "换牌",
  use_leader: "使用领袖",
  use_order: "使用指令",
  choose_card: "选择卡牌",
  choose_row: "选择排",
  choose_insert_position: "选择位置",
  discard_card: "弃牌",
  keep_hand: "完成换牌",
}

const DECISION_NAMES: Record<string, string> = {
  none: "等待",
  turn: "你的回合",
  mulligan: "换牌阶段",
  card_target: "选择目标",
  row_target: "选择排",
  insert_position: "选择排内位置",
  finished: "对局结束",
}

const TYPE_NAMES: Record<string, string> = {
  unit: "单位",
  special: "特殊",
  leader: "领袖",
}

const STATUS_NAMES: Record<string, string> = {
  bleed: "流血",
  vitality: "活力",
  poison: "中毒",
  order: "指令",
  cd: "冷却",
  count: "计数",
  shield: "护盾",
  locked: "锁定",
  veil: "帷幕",
  defender: "守卫",
  doomed: "佚亡",
}

export function actionName(kind: string): string {
  return ACTION_NAMES[kind] ?? kind
}

export function decisionName(decision: string): string {
  return DECISION_NAMES[decision] ?? decision
}

export function cardTypeName(type: string): string {
  return TYPE_NAMES[type.toLowerCase()] ?? type
}

export function actionLabel(action: GameAction): string {
  return action.label || actionName(action.kind)
}

function entityIdForObjectIndex(objectIndex: number, objects: GameObject[]): number | null {
  if (objectIndex < 0) return null
  return objects.find((item) => item.object_index === objectIndex)?.entity_id ?? null
}

export function targetEntityId(action: GameAction, objects: GameObject[]): number | null {
  return entityIdForObjectIndex(action.target_object_index, objects)
}

export function sourceEntityId(action: GameAction, objects: GameObject[]): number | null {
  return entityIdForObjectIndex(action.source_object_index, objects)
}

export function targetRowId(action: GameAction): number | null {
  return action.target_row >= 0 ? action.target_row : null
}

export function targetSideId(action: GameAction): number | null {
  return action.target_side >= 0 ? action.target_side : null
}

export function insertPosition(action: GameAction): number | null {
  return action.insert_position >= 0 ? action.insert_position : null
}

export function actionsForHandCard(actions: GameAction[], card: GameObject): GameAction[] {
  return actions.filter((action) => {
    if (action.card_id !== card.card_id) return false
    return action.hand_slot < 0 || action.hand_slot === card.slot
  })
}

export function formatStatus(raw: string): string {
  const [key, value] = raw.split("=")
  const name = STATUS_NAMES[key] ?? key
  if (value === undefined) return name

  if (key === "bleed" || key === "vitality") return `${name}：${value} 回合`
  if (key === "cd") return `${name}：${value}`
  return `${name}：${value}`
}
