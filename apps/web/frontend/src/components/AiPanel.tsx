import { useEffect, useState } from "react"

import type { AiAction, AiDecisionGroup, GameState } from "../types/game"
import { actionLabel } from "../utils/gameUi"

function ActionSummary({ action, index }: { action: AiAction; index?: number }) {
  return (
    <article className="ai-action">
      <strong>{index !== undefined ? `${index}. ` : ""}{actionLabel(action)}</strong>
      {action.source !== "-" && <span>来源：{action.source}</span>}
      {action.target !== "-" && <span>目标：{action.target}</span>}
      <div className="metrics">
        {action.confidence !== undefined && <span>置信度 {(action.confidence * 100).toFixed(1)}%</span>}
        {action.value !== undefined && <span>价值 {action.value.toFixed(3)}</span>}
        {action.ms !== undefined && <span>{action.ms.toFixed(1)} ms</span>}
        {action.status && <span>状态 {action.status}</span>}
      </div>
    </article>
  )
}

function HistoryGroup({ group, number }: { group: AiDecisionGroup; number: number }) {
  return (
    <section className="ai-history-group">
      <header>
        <strong>AI 行动 #{number}</strong>
        <span>第 {group.round} 局 · Turn {group.turn}</span>
      </header>

      {group.triggeredBy && (
        <p className="ai-history-trigger">玩家动作：{actionLabel(group.triggeredBy)}</p>
      )}

      <div className="ai-history-actions">
        {group.actions.map((action, index) => (
          <ActionSummary
            key={`${group.id}-${index}-${action.index}-${action.stable_hash}`}
            action={action}
            index={index + 1}
          />
        ))}
      </div>
    </section>
  )
}

export function AiPanel({ game, history }: { game: GameState; history: AiDecisionGroup[] }) {
  const [showHistory, setShowHistory] = useState(false)
  const [historyIndex, setHistoryIndex] = useState(Math.max(0, history.length - 1))
  const latest = game.last_ai_actions.at(-1)
  const totalDecisions = history.reduce((sum, group) => sum + group.actions.length, 0)

  useEffect(() => {
    setHistoryIndex(Math.max(0, history.length - 1))
  }, [history.length])

  const current = history[historyIndex]

  return (
    <aside className="panel ai-panel">
      <h2>AI 决策过程</h2>
      <dl>
        <div><dt>模型版本</dt><dd>Update {game.checkpoint_update}</dd></div>
        <div><dt>推理设备</dt><dd>{game.device}</dd></div>
      </dl>

      <h3>AI 最近一次实际动作</h3>
      {latest ? <ActionSummary action={latest} /> : <span className="empty">本局尚未发生 AI 实际动作。</span>}

      <button
        type="button"
        className="ai-history-button"
        onClick={() => setShowHistory((value) => !value)}
      >
        {showHistory ? "收起 AI 决策过程" : "查看 AI 决策过程"}
        <span>{history.length} 次行动 / {totalDecisions} 个内部决策</span>
      </button>

      {showHistory && (
        <div className="ai-history">
          {current ? (
            <>
              <div className="ai-history-nav">
                <button
                  type="button"
                  aria-label="上一次 AI 行动"
                  disabled={historyIndex <= 0}
                  onClick={() => setHistoryIndex((value) => Math.max(0, value - 1))}
                >
                  ←
                </button>
                <span>{historyIndex + 1} / {history.length}</span>
                <button
                  type="button"
                  aria-label="下一次 AI 行动"
                  disabled={historyIndex >= history.length - 1}
                  onClick={() => setHistoryIndex((value) => Math.min(history.length - 1, value + 1))}
                >
                  →
                </button>
              </div>
              <HistoryGroup group={current} number={historyIndex + 1} />
            </>
          ) : (
            <p className="empty">本局还没有记录到 AI 决策。</p>
          )}
          <p className="ai-history-note">每个“AI 行动”包含该次获得控制权后的全部内部决策；刷新页面后只能恢复 Core 返回的最近一次 AI 行动。</p>
        </div>
      )}
    </aside>
  )
}
