import type { GameAction, GameObject, RowEffect } from "../types/game"
import { BoardRow } from "./BoardRow"

const MELEE = 0
const RANGED = 1

function row(objects: GameObject[], controller: number, rowId: number) {
  return objects.filter(
    (item) => item.controller === controller && item.zone_name !== "Hand" && item.zone_name !== "Leader" && item.row === rowId,
  )
}

interface GameBoardProps {
  objects: GameObject[]
  rowEffects?: RowEffect[]
  targetEntityIds?: Set<number>
  targetRows?: Set<string>
  insertActionsByRow?: Record<string, GameAction[]>
  onCardTarget?: (card: GameObject) => void
  onRowTarget?: (side: number, row: number) => void
  onInsertPosition?: (action: GameAction) => void
  onDetails?: (card: GameObject) => void
  manualTest?: boolean
}

export function GameBoard({
  objects,
  rowEffects = [],
  targetEntityIds = new Set(),
  targetRows = new Set(),
  insertActionsByRow = {},
  onCardTarget,
  onRowTarget,
  onInsertPosition,
  onDetails,
  manualTest = false,
}: GameBoardProps) {
  const isRowTarget = (side: number, rowId: number) => targetRows.has(`${side}:${rowId}`)
  const insertActions = (side: number, rowId: number) => insertActionsByRow[`${side}:${rowId}`] ?? []
  const effects = (side: number, rowId: number) => rowEffects.filter((effect) => effect.side === side && effect.row === rowId)

  return (
    <div className="board">
      <BoardRow
        title={manualTest ? "P1 远程排" : "AI 远程排"}
        cards={row(objects, 1, RANGED)}
        effects={effects(1, RANGED)}
        targetEntityIds={targetEntityIds}
        rowTargetable={isRowTarget(1, RANGED)}
        insertActions={insertActions(1, RANGED)}
        onCardTarget={onCardTarget}
        onRowTarget={() => onRowTarget?.(1, RANGED)}
        onInsertPosition={onInsertPosition}
        onDetails={onDetails}
      />
      <BoardRow
        title={manualTest ? "P1 近战排" : "AI 近战排"}
        cards={row(objects, 1, MELEE)}
        effects={effects(1, MELEE)}
        targetEntityIds={targetEntityIds}
        rowTargetable={isRowTarget(1, MELEE)}
        insertActions={insertActions(1, MELEE)}
        onCardTarget={onCardTarget}
        onRowTarget={() => onRowTarget?.(1, MELEE)}
        onInsertPosition={onInsertPosition}
        onDetails={onDetails}
      />
      <div className="board__divider">战 场</div>
      <BoardRow
        title={manualTest ? "P0 近战排" : "我方近战排"}
        cards={row(objects, 0, MELEE)}
        effects={effects(0, MELEE)}
        targetEntityIds={targetEntityIds}
        rowTargetable={isRowTarget(0, MELEE)}
        insertActions={insertActions(0, MELEE)}
        onCardTarget={onCardTarget}
        onRowTarget={() => onRowTarget?.(0, MELEE)}
        onInsertPosition={onInsertPosition}
        onDetails={onDetails}
      />
      <BoardRow
        title={manualTest ? "P0 远程排" : "我方远程排"}
        cards={row(objects, 0, RANGED)}
        effects={effects(0, RANGED)}
        targetEntityIds={targetEntityIds}
        rowTargetable={isRowTarget(0, RANGED)}
        insertActions={insertActions(0, RANGED)}
        onCardTarget={onCardTarget}
        onRowTarget={() => onRowTarget?.(0, RANGED)}
        onInsertPosition={onInsertPosition}
        onDetails={onDetails}
      />
    </div>
  )
}
