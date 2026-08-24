import { Fragment } from "react"

import type { GameAction, GameObject, RowEffect } from "../types/game"
import { insertPosition } from "../utils/gameUi"
import { Card } from "./Card"

interface BoardRowProps {
  title: string
  cards: GameObject[]
  effects?: RowEffect[]
  targetEntityIds?: Set<number>
  rowTargetable?: boolean
  insertActions?: GameAction[]
  onCardTarget?: (card: GameObject) => void
  onRowTarget?: () => void
  onInsertPosition?: (action: GameAction) => void
  onDetails?: (card: GameObject) => void
}

export function BoardRow({
  title,
  cards,
  effects = [],
  targetEntityIds = new Set(),
  rowTargetable = false,
  insertActions = [],
  onCardTarget,
  onRowTarget,
  onInsertPosition,
  onDetails,
}: BoardRowProps) {
  const orderedCards = [...cards].sort((a, b) => a.slot - b.slot)
  const score = orderedCards.reduce((sum, card) => sum + card.power, 0)
  const insertByPosition = new Map<number, GameAction>()
  for (const action of insertActions) {
    const position = insertPosition(action)
    if (position !== null) insertByPosition.set(position, action)
  }
  const inserting = insertByPosition.size > 0
  const effectClasses = effects.map((effect) => `board-row--weather-${effect.id.replace(/_/g, "-")}`).join(" ")

  const effectIcon = (effectId: string) => {
    if (effectId === "frost") return "❄"
    if (effectId === "blood_moon") return "☾"
    return "◆"
  }

  const effectHint = (effect: RowEffect) => {
    if (effect.id === "frost") return `霜：剩余 ${effect.duration} 回合；在对应回合开始时结算。`
    if (effect.id === "blood_moon") return `血月：剩余 ${effect.duration} 回合；在对应回合结束时结算。`
    return `${effect.name}：剩余 ${effect.duration} 回合。`
  }

  const slotButton = (position: number) => {
    const action = insertByPosition.get(position)
    if (!action) return null
    return (
      <button
        key={`insert-${position}-${action.index}`}
        type="button"
        className="insert-slot"
        title={`插入到第 ${position + 1} 个位置`}
        onClick={() => onInsertPosition?.(action)}
      >
        <span>＋</span>
        <small>{position + 1}</small>
      </button>
    )
  }

  return (
    <section className={`board-row ${effectClasses} ${rowTargetable ? "board-row--targetable" : ""} ${inserting ? "board-row--inserting" : ""}`}>
      <header>
        <div className="board-row__identity">
          <span>{title}</span>
          {effects.length > 0 && (
            <div className="row-effects" aria-label={`${title}天气`}>
              {effects.map((effect) => (
                <span
                  key={`${effect.id}-${effect.duration}`}
                  className={`row-effect row-effect--${effect.id.replace(/_/g, "-")}`}
                  title={effectHint(effect)}
                >
                  <span className="row-effect__icon" aria-hidden="true">{effectIcon(effect.id)}</span>
                  <strong>{effect.name}</strong>
                  <span className="row-effect__duration">{effect.duration} 回合</span>
                </span>
              ))}
            </div>
          )}
        </div>
        <div className="board-row__tools">
          <strong>{score}</strong>
          {rowTargetable && <button type="button" onClick={onRowTarget}>选择此排</button>}
        </div>
      </header>
      <div className={`card-strip ${inserting ? "card-strip--insert-mode" : ""}`}>
        {inserting ? (
          <>
            {orderedCards.map((card, index) => (
              <Fragment key={card.entity_id}>
                {slotButton(index)}
                <Card
                  card={card}
                  targetable={targetEntityIds.has(card.entity_id)}
                  onClick={targetEntityIds.has(card.entity_id) ? () => onCardTarget?.(card) : undefined}
                  onDetails={onDetails}
                />
              </Fragment>
            ))}
            {slotButton(orderedCards.length)}
          </>
        ) : orderedCards.length ? (
          orderedCards.map((card) => (
            <Card
              key={card.entity_id}
              card={card}
              targetable={targetEntityIds.has(card.entity_id)}
              onClick={targetEntityIds.has(card.entity_id) ? () => onCardTarget?.(card) : undefined}
              onDetails={onDetails}
            />
          ))
        ) : (
          <span className="empty">空</span>
        )}
      </div>
    </section>
  )
}
