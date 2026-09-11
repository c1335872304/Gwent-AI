import { useEffect, useState } from "react"

import { previewTeacherTurn } from "../api/teacher"
import type { GameState, TeacherLevel, TeacherResponse, TeacherTurnResponse } from "../types/game"

const levelLabels: Record<TeacherLevel, string> = {
  beginner: "新手",
  intermediate: "进阶",
  advanced: "高级",
}

const stoppedLabels: Record<string, string> = {
  action_chain_resolved: "本次指导动作及必要选择已完成",
  human_turn_finished: "当前回合结束",
  game_finished: "对局结束",
  budget_exceeded: "达到安全步数上限",
}

function TurnStep({ step, index, choice }: { step: TeacherResponse; index: number; choice?: boolean }) {
  return (
    <article className={choice ? "teacher-turn-step teacher-turn-step--choice" : "teacher-turn-step"}>
      <header>
        <strong>{choice ? `必要选择 · ${step.action_label}` : `指导动作 · ${step.action_label}`}</strong>
        <span>{choice ? `第 ${index + 1} 步` : "第 1 步"} · 只读推演</span>
      </header>
      <p>{step.explanation}</p>
      <div className="teacher-metrics">
        {step.policy_probability !== null && <span>动作概率 {(step.policy_probability * 100).toFixed(1)}%</span>}
        {step.state_value !== null && <span>局面价值 {step.state_value.toFixed(3)}</span>}
      </div>
      {step.grounded_facts.length > 0 && (
        <details className="teacher-details">
          <summary>查看这一步的解释依据</summary>
          <ul>
            {step.grounded_facts.map((fact, factIndex) => <li key={`${factIndex}-${fact}`}>{fact}</li>)}
          </ul>
        </details>
      )}
    </article>
  )
}

function TurnAnswer({ response }: { response: TeacherTurnResponse }) {
  const stopped = stoppedLabels[response.stopped_reason] ?? response.stopped_reason
  const root = response.steps.find((step) => step.action_role === "root_action") ?? response.steps[0]
  const choices = response.steps.filter((step) => step !== root)
  return (
    <div className="teacher-answer">
      <h3>{response.headline}</h3>
      <p>{response.explanation}</p>
      <p className="teacher-turn-stop">停止原因：{stopped}</p>
      <div className="teacher-turn-steps">
        {root && <TurnStep key={`${root.decision_serial ?? 0}-${root.action_label}`} step={root} index={0} />}
        {choices.length > 0 && (
          <div className="teacher-required-choices">
            <span>完成此动作所需的 Core 选择</span>
            {choices.map((step, index) => (
              <TurnStep
                key={`${step.decision_serial ?? index + 1}-${step.action_label}`}
                step={step}
                index={index + 1}
                choice
              />
            ))}
          </div>
        )}
      </div>
      {response.caveats.length > 0 && <p className="teacher-caveat">{response.caveats.at(-1)}</p>}
    </div>
  )
}

export function TeacherPanel({ game }: { game: GameState }) {
  const canPreview = game.mode === "human_vs_ai"
    && !game.summary.done
    && game.summary.actor === 0
    && game.summary.decision === "turn"
    && game.actions.length > 0
  const [level, setLevel] = useState<TeacherLevel>("beginner")
  const [response, setResponse] = useState<TeacherTurnResponse | null>(null)
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState<string | null>(null)

  useEffect(() => {
    if (!canPreview) {
      setResponse(null)
      setError(null)
      setLoading(false)
      return
    }

    let cancelled = false
    setLoading(true)
    setError(null)
    void previewTeacherTurn(level, game.match_id, game.revision)
      .then((result) => {
        if (cancelled) return
        if (result.stale || result.base_match_id !== game.match_id || result.base_revision !== game.revision) {
          // A later real step/new game won the race.  Its game prop will
          // trigger a new request, so never let this response overwrite it.
          setResponse(null)
          return
        }
        if (!result.ok || !result.response) {
          setResponse(null)
          setError(result.error ?? "教师暂时无法推演当前回合。")
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
  }, [canPreview, level, game.match_id, game.revision])

  const unavailableReason = game.mode !== "human_vs_ai"
    ? "双人测试模式不调用教师推演。"
    : game.summary.done
      ? "对局已经结束，无法推演当前回合。"
      : game.summary.actor !== 0
        ? "正在等待人类获得控制权后再推演。"
        : game.summary.decision !== "turn"
          ? "请先完成当前的换牌或必选步骤，再生成 AI 指导动作。"
        : "当前没有可推演的合法动作。"

  return (
    <section className="panel teacher-panel">
      <div className="teacher-panel__header">
        <div>
          <span className="teacher-kicker">READ-ONLY TURN SIMULATION</span>
          <h2>AI 教师</h2>
        </div>
        <select
          aria-label="教师解释级别"
          value={level}
          disabled={loading || !canPreview}
          onChange={(event) => setLevel(event.target.value as TeacherLevel)}
        >
          {(Object.keys(levelLabels) as TeacherLevel[]).map((item) => (
            <option key={item} value={item}>{levelLabels[item]}</option>
          ))}
        </select>
      </div>

      <p className="teacher-mode-note">当前局面的一个 AI 指导动作及其必要选择 · 只读，不会修改真实对局。</p>

      {!canPreview ? (
        <p className="empty">{unavailableReason}</p>
      ) : loading ? (
        <p className="muted">正在根据当前局面推演指导动作……</p>
      ) : error ? (
        <div className="teacher-unavailable">
          <strong>教师暂不可用</strong>
          <span>{error}</span>
          <small>这不会影响游戏或 AI 决策。</small>
        </div>
      ) : response ? <TurnAnswer response={response} /> : null}
    </section>
  )
}
