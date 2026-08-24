import type { GameAction } from "../types/game"
import { actionLabel } from "../utils/gameUi"

interface ActionPanelProps {
  actions: GameAction[]
  busy: boolean
  onAction: (optionIndex: number) => void
}

export function ActionPanel({ actions, busy, onAction }: ActionPanelProps) {
  return (
    <aside className="panel legal-actions-panel">
      <div className="legal-actions-panel__title">
        <h2>核心合法动作</h2>
        <span>{actions.length}</span>
      </div>
      <p className="muted">这里完整显示 Core 当前给出的合法动作，主要用于联调和核对规则。</p>
      <div className="action-list">
        {actions.length ? (
          actions.map((action) => (
            <button key={`${action.index}-${action.stable_hash}`} disabled={busy} onClick={() => onAction(action.index)}>
              <strong>{actionLabel(action)}</strong>
              <small>{action.target !== "-" ? `目标：${action.target}` : action.kind}</small>
            </button>
          ))
        ) : (
          <span className="empty">当前没有玩家操作。</span>
        )}
      </div>
    </aside>
  )
}
