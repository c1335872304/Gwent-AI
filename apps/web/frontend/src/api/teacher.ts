import { requestJson } from "./client"
import type { TeacherExplainResult, TeacherLevel } from "../types/game"

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
