import type { GameObject } from "../types/game"
import { cardTypeName, formatStatus } from "../utils/gameUi"

export function CardDetailModal({ card, onClose }: { card: GameObject | null; onClose: () => void }) {
  if (!card) return null

  return (
    <div className="modal-backdrop" onMouseDown={onClose}>
      <section className="card-detail" onMouseDown={(event) => event.stopPropagation()}>
        <header>
          <div>
            <span className="eyebrow">卡牌详情</span>
            <h2>{card.name}</h2>
          </div>
          <button type="button" className="icon-button" onClick={onClose}>×</button>
        </header>

        <div className="card-detail__stats">
          <div><span>当前战力</span><strong>{card.power}</strong></div>
          <div><span>护甲</span><strong>{card.armor}</strong></div>
          <div><span>类型</span><strong>{cardTypeName(card.type)}</strong></div>
        </div>

        <dl className="detail-list">
          <div><dt>位置</dt><dd>{card.zone_name} / {card.row_name}</dd></div>
          <div><dt>所属玩家</dt><dd>P{card.owner}</dd></div>
          <div><dt>控制玩家</dt><dd>P{card.controller}</dd></div>
          <div><dt>Card ID</dt><dd>{card.card_id}</dd></div>
        </dl>

        <h3>卡牌能力</h3>
        {card.ability_text ? (
          <p className="card-detail__ability">{card.ability_text}</p>
        ) : (
          <p className="muted">当前卡牌没有登记能力文本。</p>
        )}

        <h3>当前状态</h3>
        {card.status.length ? (
          <div className="status-list">
            {card.status.map((status) => <span key={status}>{formatStatus(status)}</span>)}
          </div>
        ) : (
          <p className="muted">当前没有持续状态。</p>
        )}

        <p className="detail-note">能力文本来自 Core 的卡牌数据清单；战力、护甲和状态是本局实时值。</p>
      </section>
    </div>
  )
}
