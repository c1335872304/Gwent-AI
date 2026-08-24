import type { GameObject } from "../types/game"
import { cardTypeName, formatStatus } from "../utils/gameUi"

interface CardProps {
  card: GameObject
  playable?: boolean
  targetable?: boolean
  selected?: boolean
  onClick?: () => void
  onDetails?: (card: GameObject) => void
}

export function Card({
  card,
  playable = false,
  targetable = false,
  selected = false,
  onClick,
  onDetails,
}: CardProps) {
  const interactive = Boolean(onClick)

  return (
    <article
      className={`card ${playable ? "card--playable" : ""} ${targetable ? "card--targetable" : ""} ${selected ? "card--selected" : ""}`}
    >
      <button
        className="card__main"
        type="button"
        disabled={!interactive}
        onClick={onClick}
        title={targetable ? "选择这个目标" : selected ? "取消选择" : interactive ? "选择这张牌" : card.name}
      >
        <span className="card__power">{card.power}</span>
        <strong>{card.name}</strong>
        <small>{cardTypeName(card.type)}</small>
        {card.armor > 0 && <small>护甲 {card.armor}</small>}
        {card.status.length > 0 && (
          <span className="card__status-short">
            {formatStatus(card.status[0])}{card.status.length > 1 ? ` +${card.status.length - 1}` : ""}
          </span>
        )}
      </button>
      <button className="card__details" type="button" onClick={() => onDetails?.(card)}>
        详情
      </button>
    </article>
  )
}
