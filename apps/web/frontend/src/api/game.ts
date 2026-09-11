import { requestJson } from "./client"
import type { GameMode, GameState, HealthResponse } from "../types/game"

const jsonHeaders = { "Content-Type": "application/json" }

export function getHealth(): Promise<HealthResponse> {
  return requestJson<HealthResponse>("/api/health")
}

export function getGameState(): Promise<GameState> {
  return requestJson<GameState>("/api/game/state")
}

export function newGame(
  seed = Date.now(),
  player0DeckId = 0,
  player1DeckId = 0,
  mode: GameMode = "human_vs_ai",
): Promise<GameState> {
  return requestJson<GameState>("/api/game/new", {
    method: "POST",
    headers: jsonHeaders,
    body: JSON.stringify({
      seed,
      starting_player_id: -1,
      player0_deck_id: player0DeckId,
      player1_deck_id: player1DeckId,
      mode,
    }),
  })
}

export function step(optionIndex: number, matchId: string, expectedRevision: number): Promise<GameState> {
  return requestJson<GameState>("/api/game/step", {
    method: "POST",
    headers: jsonHeaders,
    body: JSON.stringify({
      option_index: optionIndex,
      match_id: matchId,
      expected_revision: expectedRevision,
    }),
  })
}
