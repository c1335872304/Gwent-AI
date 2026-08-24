import { useEffect, useMemo, useState } from "react"

import { explainAiAction } from "../api/teacher"
import type { GameState, TeacherLevel, TeacherResponse } from "../types/game"

const levelLabels: Record<TeacherLevel, string> = {
  beginner: "新手",
  intermediate: "进阶",
  advanced: "高级",
}

export function TeacherPanel({ game }: { game: GameState }) {
  const actionCount = game.last_ai_actions.length
  const signature = useMemo(
    () => game.last_ai_actions.map((action) => `${action.index}:${action.stable_hash}`).join("|"),
    [game.last_ai_actions],
  )
  const [level, setLevel] = useState<TeacherLevel>("beginner")
  const [position, setPosition] = useState(0)
  const [response, setResponse] = useState<TeacherResponse | null>(null)
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState<string | null>(null)

  useEffect(() => {
    setPosition(0)
  }, [signature])

  useEffect(() => {
    if (game.mode !== "human_vs_ai" || actionCount === 0) {
      setResponse(null)
      setError(null)
      return
    }

    let cancelled = false
    setLoading(true)
    setError(null)
    void explainAiAction(position, level)
      .then((result) => {
        if (cancelled) return
        if (!result.ok || !result.response) {
          setResponse(null)
          setError(result.error ?? "教师暂时无法解释这一步。")
          return
        }
        setResponse(result.response)
      })
      .catch((exc) => {
        if (cancelled) return
        setResponse(null)
        setError(exc instanceof Error ? exc.message : String(exc))
      })
      .finally(() => {
        if (!cancelled) setLoading(false)
      })

    return () => {
      cancelled = true
    }
  }, [actionCount, game.mode, level, position, signature])

  return (
    <section className="panel teacher-panel">
      <div className="teacher-panel__header">
        <div>
          <span className="teacher-kicker">READ-ONLY EXPLANATION</span>
          <h2>AI 教师</h2>
        </div>
        <select
          aria-label="教师解释级别"
          value={level}
          disabled={loading || actionCount === 0}
          onChange={(event) => setLevel(event.target.value as TeacherLevel)}
        >
          {(Object.keys(levelLabels) as TeacherLevel[]).map((item) => (
            <option key={item} value={item}>{levelLabels[item]}</option>
          ))}
        </select>
      </div>

      {actionCount > 1 && (
        <div className="teacher-step-nav">
          <button
            type="button"
            disabled={position <= 0 || loading}
            onClick={() => setPosition((value) => Math.max(0, value - 1))}
          >
            ←
          </button>
          <span>内部决策 {position + 1} / {actionCount}</span>
          <button
            type="button"
            disabled={position >= actionCount - 1 || loading}
            onClick={() => setPosition((value) => Math.min(actionCount - 1, value + 1))}
          >
            →
          </button>
        </div>
      )}

      {game.mode !== "human_vs_ai" ? (
        <p className="empty">双人测试模式不调用教师。</p>
      ) : actionCount === 0 ? (
        <p className="empty">等待 AI 完成一次动作后，这里会给出教学解释。</p>
      ) : loading ? (
        <p className="muted">正在整理这一步的公开证据……</p>
      ) : error ? (
        <div className="teacher-unavailable">
          <strong>教师暂不可用</strong>
          <span>{error}</span>
          <small>这不会影响游戏或 AI 决策。</small>
        </div>
      ) : response ? (
        <div className="teacher-answer">
          <h3>{response.headline}</h3>
          <p>{response.explanation}</p>

          <div className="teacher-metrics">
            {response.policy_probability !== null && (
              <span>动作概率 {(response.policy_probability * 100).toFixed(1)}%</span>
            )}
            {response.state_value !== null && (
              <span>局面价值 {response.state_value.toFixed(3)}</span>
            )}
          </div>

          {response.grounded_facts.length > 0 && (
            <details className="teacher-details">
              <summary>查看解释依据</summary>
              <ul>
                {response.grounded_facts.map((fact, index) => <li key={`${index}-${fact}`}>{fact}</li>)}
              </ul>
            </details>
          )}

          {response.caveats.length > 0 && (
            <p className="teacher-caveat">{response.caveats.at(-1)}</p>
          )}
        </div>
      ) : null}
    </section>
  )
}
