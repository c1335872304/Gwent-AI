import { useEffect, useMemo, useRef, useState } from "react"

import { getGameState, newGame, step } from "../api/game"
import { HttpError } from "../api/client"
import { ActionPanel } from "../components/ActionPanel"
import { AiPanel } from "../components/AiPanel"
import { Card } from "../components/Card"
import { CardDetailModal } from "../components/CardDetailModal"
import { GameBoard } from "../components/GameBoard"
import { ScoreBoard } from "../components/ScoreBoard"
import { TeacherPanel } from "../components/TeacherPanel"
import type { AiDecisionGroup, GameAction, GameMode, GameObject, GameState } from "../types/game"
import {
  actionLabel,
  actionsForHandCard,
  insertPosition,
  sourceEntityId,
  targetEntityId,
  targetRowId,
  targetSideId,
} from "../utils/gameUi"

export function GamePage() {
  const [game, setGame] = useState<GameState | null>(null)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [humanDeckId, setHumanDeckId] = useState(0)
  const [aiDeckId, setAiDeckId] = useState(0)
  const [gameMode, setGameMode] = useState<GameMode>("manual_test")
  const [detailCard, setDetailCard] = useState<GameObject | null>(null)
  const [selectedCardId, setSelectedCardId] = useState<number | null>(null)
  const [pendingActions, setPendingActions] = useState<GameAction[]>([])
  const [aiHistory, setAiHistory] = useState<AiDecisionGroup[]>([])
  const [actedTurn, setActedTurn] = useState<string | null>(null)
  const aiHistoryId = useRef(0)
  const lastAiSignature = useRef("")

  function captureAiHistory(nextGame: GameState, reset = false) {
    const signature = nextGame.last_ai_actions
      .map((action) => `${action.index}:${action.stable_hash}`)
      .join("|")

    if (reset) {
      lastAiSignature.current = ""
      aiHistoryId.current = 0
    }

    if (!signature || signature === lastAiSignature.current) {
      if (reset) setAiHistory([])
      return
    }

    lastAiSignature.current = signature
    aiHistoryId.current += 1
    const group: AiDecisionGroup = {
      id: aiHistoryId.current,
      round: nextGame.summary.round,
      turn: nextGame.summary.turn,
      actions: nextGame.last_ai_actions.map((action) => ({ ...action })),
      triggeredBy: nextGame.last_human_action ? { ...nextGame.last_human_action } : null,
    }

    setAiHistory((previous) => reset ? [group] : [...previous, group])
  }

  async function load() {
    try {
      const nextGame = await getGameState()
      setGame(nextGame)
      setHumanDeckId(nextGame.decks.p0)
      setAiDeckId(nextGame.decks.p1)
      setGameMode(nextGame.mode)
      captureAiHistory(nextGame, true)
      setError(null)
    } catch (exc) {
      setError(exc instanceof Error ? exc.message : String(exc))
    }
  }

  useEffect(() => {
    void load()
  }, [])

  async function run(
    operation: () => Promise<GameState>,
    options: { resetAiHistory?: boolean; actionKind?: string; actionTurnKey?: string } = {},
  ) {
    setBusy(true)
    try {
      const nextGame = await operation()
      setGame(nextGame)
      setHumanDeckId(nextGame.decks.p0)
      setAiDeckId(nextGame.decks.p1)
      setGameMode(nextGame.mode)
      captureAiHistory(nextGame, options.resetAiHistory ?? false)
      setError(null)
      setPendingActions([])
      setSelectedCardId(null)

      if (options.actionKind && options.actionTurnKey && [
        "play_card",
        "discard_card",
        "use_order",
        "use_leader",
      ].includes(options.actionKind)) {
        setActedTurn(options.actionTurnKey)
      }
      if (options.resetAiHistory) setActedTurn(null)
    } catch (exc) {
      if (exc instanceof HttpError && exc.status === 409 && exc.code === "stale_state") {
        await load()
        setError("局面已更新，已刷新为最新合法动作。")
      } else {
        setError(exc instanceof Error ? exc.message : String(exc))
      }
    } finally {
      setBusy(false)
    }
  }

  function startNewGame() {
    void run(() => newGame(Date.now(), humanDeckId, aiDeckId, gameMode), { resetAiHistory: true })
  }

  const manualTest = game?.mode === "manual_test"
  const controlledPlayer = manualTest ? (game?.summary.actor ?? 0) : 0

  const hand = useMemo(
    () => game?.objects.filter((item) => item.owner === controlledPlayer && item.zone_name === "Hand") ?? [],
    [game, controlledPlayer],
  )

  const leaders = useMemo(
    () => game?.objects.filter((item) => item.zone_name === "Leader") ?? [],
    [game],
  )

  const humanLeader = leaders.find((item) => item.owner === 0)
  const aiLeader = leaders.find((item) => item.owner === 1)
  const currentLeader = leaders.find((item) => item.owner === controlledPlayer)

  const selectedCard = hand.find((card) => card.entity_id === selectedCardId) ?? null
  const selectedActions = game && selectedCard ? actionsForHandCard(game.actions, selectedCard) : []
  const selectedPlayActions = selectedActions.filter((action) => action.kind === "play_card")
  const selectedDiscardActions = selectedActions.filter((action) => action.kind === "discard_card")
  const selectedMulliganActions = selectedActions.filter((action) => action.kind === "mulligan")

  const isMulligan = game?.summary.decision === "mulligan"
  const keepHandAction = game?.actions.find((action) => action.kind === "keep_hand")
  const leaderActions = game?.actions.filter((action) => action.kind === "use_leader") ?? []
  const passAction = game?.actions.find((action) => action.kind === "pass")
  const endTurnAction = game?.actions.find((action) => action.kind === "end_turn")
  const currentTurnKey = game ? `${game.summary.round}:${game.summary.turn}:${game.summary.actor}` : ""
  const hasActedThisTurn = actedTurn === currentTurnKey
  const roundButtonAction = endTurnAction ?? passAction
  const roundButtonLabel = endTurnAction || hasActedThisTurn ? "结束回合" : "放弃跟牌"
  const canUseRoundButton = Boolean(
    roundButtonAction
    && game?.summary.actor === controlledPlayer
    && game.summary.decision === "turn"
    && !busy,
  )

  // Core 进入 sequential target decision 后，直接把当前合法动作映射到战场交互。
  const targetActions = useMemo(() => {
    if (!game) return []
    if (["card_target", "row_target", "insert_position"].includes(game.summary.decision)) return game.actions
    return pendingActions
  }, [game, pendingActions])

  const targetEntityIds = useMemo(() => {
    if (!game) return new Set<number>()
    return new Set(
      targetActions
        .map((action) => targetEntityId(action, game.objects))
        .filter((value): value is number => value !== null),
    )
  }, [game, targetActions])

  const targetRows = useMemo(() => {
    if (!game) return new Set<string>()
    const result = new Set<string>()
    for (const action of targetActions) {
      if (action.kind === "choose_insert_position") continue
      const row = targetRowId(action)
      if (row === null) continue
      const side = targetSideId(action)
      if (side === null) continue
      result.add(`${side}:${row}`)
    }
    return result
  }, [game, targetActions])

  const insertActionsByRow = useMemo(() => {
    const result: Record<string, GameAction[]> = {}
    for (const action of targetActions) {
      if (action.kind !== "choose_insert_position" && insertPosition(action) === null) continue
      const row = targetRowId(action)
      const position = insertPosition(action)
      const side = targetSideId(action)
      if (row === null || position === null || side === null) continue
      const key = `${side}:${row}`
      if (!result[key]) result[key] = []
      result[key].push(action)
    }
    for (const actions of Object.values(result)) {
      actions.sort((a, b) => (insertPosition(a) ?? 0) - (insertPosition(b) ?? 0))
    }
    return result
  }, [targetActions])

  const selectingInsertPosition = Object.keys(insertActionsByRow).length > 0
  const selectingTarget = targetEntityIds.size > 0 || targetRows.size > 0 || selectingInsertPosition

  const stagedSourceIds = useMemo(() => {
    if (!game) return new Set<number>()
    return new Set(
      game.actions
        .map((action) => sourceEntityId(action, game.objects))
        .filter((value): value is number => value !== null),
    )
  }, [game])

  const stagedCards = useMemo(
    () => game?.objects.filter(
      (item) => item.controller === controlledPlayer
        && item.zone_name === "Stay"
        && item.type.toLowerCase() === "unit"
        && stagedSourceIds.has(item.entity_id),
    ) ?? [],
    [game, controlledPlayer, stagedSourceIds],
  )
  const stagedCard = stagedCards[0] ?? null

  function runAction(action: GameAction) {
    if (!game || busy) return
    void run(() => step(action.index, game.match_id, game.revision), {
      actionKind: action.kind,
      actionTurnKey: currentTurnKey,
    })
  }

  function executeOrSelect(actions: GameAction[]) {
    if (!actions.length || busy || !game) return

    const hasTarget = actions.some(
      (action) => targetEntityId(action, game.objects) !== null
        || targetRowId(action) !== null
        || insertPosition(action) !== null,
    )

    if (actions.length === 1 && !hasTarget) {
      runAction(actions[0])
      return
    }

    setPendingActions(actions)
  }

  function handleHandCard(card: GameObject) {
    if (!game || busy) return

    // 如果 Core 正在要求选择一张手牌作为目标，点击手牌就是选目标。
    if (targetEntityIds.has(card.entity_id)) {
      const action = targetActions.find((item) => targetEntityId(item, game.objects) === card.entity_id)
      if (action) runAction(action)
      return
    }

    if (selectingTarget) return

    setSelectedCardId((current) => current === card.entity_id ? null : card.entity_id)
    setPendingActions([])
  }

  function handleBoardTarget(card: GameObject) {
    if (!game || busy) return
    const action = targetActions.find((item) => targetEntityId(item, game.objects) === card.entity_id)
    if (action) runAction(action)
  }

  function handleRowTarget(side: number, row: number) {
    if (busy) return
    const action = targetActions.find((item) => {
      const actionRow = targetRowId(item)
      return actionRow === row && targetSideId(item) === side && item.kind !== "choose_insert_position"
    })
    if (action) runAction(action)
  }

  function handleInsertPosition(action: GameAction) {
    if (busy) return
    runAction(action)
  }

  function handleLeader() {
    if (!game || !currentLeader || busy || !leaderActions.length) return

    const exact = leaderActions.filter((action) => {
      const source = sourceEntityId(action, game.objects)
      return source === null || source === currentLeader.entity_id
    })
    executeOrSelect(exact.length ? exact : leaderActions)
  }

  const directQuickActions = game?.actions.filter(
    (action) => ![
      "play_card",
      "mulligan",
      "discard_card",
      "use_leader",
      "choose_card",
      "choose_row",
      "choose_insert_position",
      "pass",
      "keep_hand",
      "end_turn",
    ].includes(action.kind),
  ) ?? []

  if (!game) {
    return (
      <main className="centered">
        <h1>昆特牌 AI</h1>
        <p>{error ?? "正在连接决策核心……"}</p>
        <button onClick={startNewGame}>开始新游戏</button>
      </main>
    )
  }

  const winnerText = manualTest
    ? game.summary.winner_id === 0 ? "P0 获胜" : game.summary.winner_id === 1 ? "P1 获胜" : "平局"
    : game.summary.winner_id === 0 ? "你获胜" : game.summary.winner_id === 1 ? "AI 获胜" : "平局"

  return (
    <main className="layout">
      <header className="topbar">
        <div>
          <span className="eyebrow">GWENT AI</span>
          <h1>{manualTest ? "双人卡牌测试" : "人类 vs 决策核心"}</h1>
        </div>
        <div className="topbar-actions">
          <label>
            对局模式
            <select
              value={gameMode}
              disabled={busy}
              onChange={(event) => setGameMode(event.target.value as GameMode)}
            >
              <option value="human_vs_ai">人类 vs AI</option>
              <option value="manual_test">双人测试 · 自己打自己</option>
            </select>
          </label>
          <label>
            {gameMode === "manual_test" ? "P0 卡组" : "我的卡组"}
            <select value={humanDeckId} disabled={busy} onChange={(event) => setHumanDeckId(Number(event.target.value))}>
              <option value={0}>Deck A</option>
              <option value={1}>Deck B · 白霜</option>
            </select>
          </label>
          <label>
            {gameMode === "manual_test" ? "P1 卡组" : "AI 卡组"}
            <select value={aiDeckId} disabled={busy} onChange={(event) => setAiDeckId(Number(event.target.value))}>
              <option value={0}>Deck A</option>
              <option value={1}>Deck B · 白霜</option>
            </select>
          </label>
          <button disabled={busy} onClick={startNewGame}>新游戏</button>
        </div>
      </header>

      {error && <div className="error">{error}</div>}
      {game.summary.done && <div className="result">对局结束：{winnerText}</div>}
      {selectingTarget && (
        <div className="interaction-hint">
          {selectingInsertPosition
            ? `${stagedCard ? `待打出：${stagedCard.name}。` : ""}请选择高亮的排内插入位置。位置由 Core 动态生成。`
            : stagedCard && targetRows.size > 0
              ? `卡牌效果已生成：${stagedCard.name}。请选择高亮的己方战场排。`
              : `请选择高亮的${targetEntityIds.size ? "单位/卡牌" : "战场排"}作为目标。`}
        </div>
      )}

      <ScoreBoard summary={game.summary} manualTest={manualTest} />

      <div className="main-grid">
        <aside className="left-sidebar">
          {manualTest ? (
            <section className="panel test-mode-panel">
              <h2>双人测试模式</h2>
              <p>AI 已停用。你现在控制 P0 和 P1，Core 每次只执行你点击的一个合法动作。</p>
              <strong>当前操作：P{game.summary.actor}</strong>
              <span>双方手牌仅在此测试模式中可见。</span>
            </section>
          ) : (
            <>
              <AiPanel game={game} history={aiHistory} />
              <TeacherPanel game={game} />
            </>
          )}

          <div className="leader-rail">
            <section className="leader-slot">
              <span className="leader-slot__label">{manualTest ? "P1 领袖" : "AI 领袖"}</span>
              {aiLeader ? (
                <>
                  <button
                    className={`leader-card ${manualTest && controlledPlayer === 1 && leaderActions.length ? "leader-card--ready" : ""}`}
                    type="button"
                    disabled={busy || !manualTest || controlledPlayer !== 1 || !leaderActions.length}
                    onClick={handleLeader}
                  >
                    <strong>{aiLeader.name}</strong>
                    <small>{manualTest && controlledPlayer === 1 && leaderActions.length ? "点击使用领袖能力" : "查看详情"}</small>
                  </button>
                  <button className="leader-detail" type="button" onClick={() => setDetailCard(aiLeader)}>详情</button>
                </>
              ) : <span className="empty">无领袖信息</span>}
            </section>

            <section className="leader-slot leader-slot--human">
              <span className="leader-slot__label">{manualTest ? "P0 领袖" : "我的领袖"}</span>
              {humanLeader ? (
                <>
                  <button
                    className={`leader-card ${controlledPlayer === 0 && leaderActions.length ? "leader-card--ready" : ""}`}
                    type="button"
                    disabled={busy || controlledPlayer !== 0 || !leaderActions.length}
                    onClick={handleLeader}
                  >
                    <strong>{humanLeader.name}</strong>
                    <small>{controlledPlayer === 0 && leaderActions.length ? "点击使用领袖能力" : "当前不可使用"}</small>
                  </button>
                  <button className="leader-detail" type="button" onClick={() => setDetailCard(humanLeader)}>详情</button>
                </>
              ) : <span className="empty">无领袖信息</span>}
            </section>
          </div>
        </aside>

        <section className="play-column">
          <GameBoard
            objects={game.objects}
            rowEffects={game.row_effects}
            targetEntityIds={targetEntityIds}
            targetRows={targetRows}
            insertActionsByRow={insertActionsByRow}
            onCardTarget={handleBoardTarget}
            onRowTarget={handleRowTarget}
            onInsertPosition={handleInsertPosition}
            onDetails={setDetailCard}
            manualTest={manualTest}
          />

          {stagedCards.length > 0 && (
            <section className="staged-play">
              <header>
                <div>
                  <h2>效果生成 · 待打出</h2>
                  <span>卡牌已经由 Core 生成，请继续选择部署排和具体位置。</span>
                </div>
              </header>
              <div className="card-strip">
                {stagedCards.map((card) => (
                  <Card key={card.entity_id} card={card} selected onDetails={setDetailCard} />
                ))}
              </div>
            </section>
          )}

          <section className="hand">
            <header>
              <div>
                <h2>{manualTest ? `P${controlledPlayer} 手牌` : "我的手牌"}</h2>
                <span>{hand.length} 张{selectedCard ? ` · 已选：${selectedCard.name}` : ""}</span>
              </div>
              <div className="hand-tools">
                {isMulligan ? (
                  <>
                    <button
                      type="button"
                      className="tool-button tool-button--discard"
                      disabled={busy || selectingTarget || selectedMulliganActions.length === 0}
                      onClick={() => executeOrSelect(selectedMulliganActions)}
                    >
                      换牌
                    </button>
                    <button
                      type="button"
                      className="tool-button"
                      disabled={busy || !keepHandAction}
                      onClick={() => keepHandAction && runAction(keepHandAction)}
                    >
                      完成换牌
                    </button>
                  </>
                ) : (
                  <>
                    <button
                      type="button"
                      className="tool-button tool-button--play"
                      disabled={busy || selectingTarget || selectedPlayActions.length === 0}
                      onClick={() => executeOrSelect(selectedPlayActions)}
                    >
                      出牌
                    </button>
                    <button
                      type="button"
                      className="tool-button tool-button--discard"
                      disabled={busy || selectingTarget || selectedDiscardActions.length === 0}
                      onClick={() => executeOrSelect(selectedDiscardActions)}
                    >
                      丢弃
                    </button>
                    <button
                      type="button"
                      className="tool-button pass-button"
                      disabled={!canUseRoundButton}
                      title={
                        roundButtonLabel === "结束回合" && !endTurnAction
                          ? "本回合已经执行过动作，但还需要先打出或丢弃一张手牌"
                          : roundButtonLabel
                      }
                      onClick={() => roundButtonAction && runAction(roundButtonAction)}
                    >
                      {roundButtonLabel}
                    </button>
                  </>
                )}
              </div>
            </header>

            <div className="card-strip">
              {hand.map((card) => {
                const actions = actionsForHandCard(game.actions, card)
                const selectableKinds = isMulligan ? ["mulligan"] : ["play_card", "discard_card"]
                const selectable = actions.some((action) => selectableKinds.includes(action.kind))
                const targetable = targetEntityIds.has(card.entity_id)

                return (
                  <Card
                    key={card.entity_id}
                    card={card}
                    playable={selectable && !busy && !selectingTarget}
                    targetable={targetable}
                    selected={selectedCardId === card.entity_id}
                    onClick={(selectable && !selectingTarget) || targetable ? () => handleHandCard(card) : undefined}
                    onDetails={setDetailCard}
                  />
                )
              })}
            </div>
          </section>

          {directQuickActions.length > 0 && (
            <section className="quick-actions">
              {directQuickActions.map((action) => (
                <button key={`${action.index}-${action.stable_hash}`} type="button" disabled={busy} onClick={() => runAction(action)}>
                  {actionLabel(action)}
                </button>
              ))}
            </section>
          )}
        </section>

        <aside className="right-sidebar">
          <ActionPanel actions={game.actions} busy={busy} onAction={(index) => {
            const action = game.actions.find((item) => item.index === index)
            if (action) runAction(action)
          }} />
        </aside>
      </div>

      <CardDetailModal card={detailCard} onClose={() => setDetailCard(null)} />
    </main>
  )
}
