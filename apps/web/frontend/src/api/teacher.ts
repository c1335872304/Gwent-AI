import { requestJson } from "./client"
import type { TeacherExplainResult, TeacherLevel, TeacherTurnPreviewResult } from "../types/game"

const jsonHeaders = { "Content-Type": "application/json" }

export function explainAiAction(
  actionPosition: number,
  level: TeacherLevel,
): Promise<TeacherExplainResult> {
  return requestJson<TeacherExplainResult>("/api/teacher/explain", {
    method: "POST",
    headers: jsonHeaders,
    body: JSON.stringify({
      action_position: actionPosition,
      level,
      top_k: 3,
    }),
  })
}

export function previewTeacherTurn(
  level: TeacherLevel,
  matchId: string,
  expectedRevision: number,
): Promise<TeacherTurnPreviewResult> {
  return requestJson<TeacherTurnPreviewResult>("/api/teacher/preview-turn", {
    method: "POST",
    headers: jsonHeaders,
    body: JSON.stringify({
      level,
      top_k: 3,
      match_id: matchId,
      expected_revision: expectedRevision,
    }),
  })
}
