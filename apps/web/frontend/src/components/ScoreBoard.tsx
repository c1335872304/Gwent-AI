import type { GameSummary } from "../types/game"
import { decisionName } from "../utils/gameUi"

interface ScoreBoardProps {
  summary: GameSummary
  manualTest?: boolean
}

export function ScoreBoard({ summary, manualTest = false }: ScoreBoardProps) {
  return (
    <section className="scoreboard">
      <div>
        <small>{manualTest ? "P0" : "你"}</small>
        <strong>{summary.p0.score}</strong>
        <span>手牌 {summary.p0.hand} · 小局 {summary.p0.wins}{summary.p0.passed ? " · 已放弃跟牌" : ""}</span>
      </div>
      <div className="scoreboard__middle">
        <strong>第 {summary.round} 局</strong>
        <span>回合 {summary.turn}</span>
        <span>{decisionName(summary.decision)}</span>
        {manualTest && !summary.done && <span className="actor-badge">当前操作：P{summary.actor}</span>}
      </div>
      <div>
        <small>{manualTest ? "P1" : "AI"}</small>
        <strong>{summary.p1.score}</strong>
        <span>手牌 {summary.p1.hand} · 小局 {summary.p1.wins}{summary.p1.passed ? " · 已放弃跟牌" : ""}</span>
      </div>
    </section>
  )
}
