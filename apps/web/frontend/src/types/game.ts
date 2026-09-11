export const CORE_API_VERSION = 1

export type GameMode = "human_vs_ai" | "manual_test"

export interface PlayerSummary {
  score: number
  hand: number
  wins: number
  passed: boolean
}

export interface GameSummary {
  round: number
  turn: number
  actor: number
  decision_kind: number
  decision: string
  done: boolean
  winner_id: number
  p0: PlayerSummary
  p1: PlayerSummary
}

export interface GameObject {
  object_index: number
  entity_id: number
  card_id: number
  name: string
  type: string
  ability_text: string
  owner: number
  controller: number
  zone: number
  zone_name: string
  row: number
  row_name: string
  slot: number
  power: number
  armor: number
  status: string[]
}

export interface RowEffect {
  side: number
  row: number
  id: string
  name: string
  duration: number
}

export interface GameAction {
  index: number
  kind_id: number
  kind: string
  label: string
  card_id: number
  source: string
  target: string
  hand_slot: number

  // Core emits uint64 stable hashes as decimal strings so JS never loses bits.
  stable_hash: string

  // Structured Core contract. -1 means the field is not applicable.
  // UI must never reconstruct these values by parsing source/target labels.
  source_object_index: number
  target_object_index: number
  target_side: number
  target_zone: number
  target_row: number
  insert_position: number
}

export interface AiAction extends GameAction {
  confidence?: number
  value?: number
  ms?: number
  status?: string
  reward_p0?: number
  reward_p1?: number
}

// One AI turn may contain multiple sequential choose_* decisions.
export interface AiDecisionGroup {
  id: number
  round: number
  turn: number
  actions: AiAction[]
  triggeredBy: GameAction | null
}

export interface DeckSelection {
  p0: number
  p1: number
}

export interface GameState {
  api_version: number
  match_id: string
  revision: number
  summary: GameSummary
  objects: GameObject[]
  row_effects: RowEffect[]
  actions: GameAction[]
  last_human_action: AiAction | null
  last_ai_actions: AiAction[]
  checkpoint: string
  checkpoint_update: number
  device: string
  decks: DeckSelection
  mode: GameMode
}

export interface HealthResponse {
  ok: boolean
  service: string
  core?: {
    ok: boolean
    api_version: number
    schema_version: number
    checkpoint: string
    checkpoint_update: number
    device: string
    actor: number
  }
  core_error?: string
}

export type TeacherLevel = "beginner" | "intermediate" | "advanced"

export interface TeacherAlternative {
  option_index: number
  label: string
  probability: number | null
}

export interface TeacherResponse {
  schema_version: string
  decision_serial: number | null
  level: TeacherLevel
  headline: string
  explanation: string
  action_label: string
  policy_probability: number | null
  state_value: number | null
  action_role?: "root_action" | "required_choice" | null
  parent_decision_serial?: number | null
  alternatives: TeacherAlternative[]
  grounded_facts: string[]
  caveats: string[]
  prompt: string | null
}

export interface TeacherExplainResult {
  ok: boolean
  response?: TeacherResponse | null
  error?: string | null
}

export interface TeacherTurnResponse {
  schema_version: string
  level: TeacherLevel
  headline: string
  explanation: string
  stopped_reason: string
  steps: TeacherResponse[]
  root_decision_serial?: number | null
  boundary?: "one_root_action_with_required_choices" | string | null
  grounded_facts: string[]
  caveats: string[]
}

export interface TeacherTurnPreviewResult {
  ok: boolean
  base_match_id?: string | null
  base_revision?: number | null
  stale?: boolean
  response?: TeacherTurnResponse | null
  error?: string | null
}
