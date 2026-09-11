from __future__ import annotations

from typing import Any, Literal

from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, ConfigDict, Field

from .agent import TEACHER_RESPONSE_SCHEMA_VERSION, TeacherAgent
from .models import TeacherRequest, TeacherTurnRequest


class ApiModel(BaseModel):
    model_config = ConfigDict(extra="forbid")


class ExplainRequest(ApiModel):
    decision_packet: dict[str, Any]
    public_state: dict[str, Any] = Field(default_factory=dict)
    level: Literal["beginner", "intermediate", "advanced"] = "beginner"
    language: str = "zh-CN"
    top_k: int = Field(default=3, ge=1, le=8)


class ExplainResponse(ApiModel):
    response: dict[str, Any]


class ExplainTurnRequest(ApiModel):
    turn_trace: dict[str, Any]
    level: Literal["beginner", "intermediate", "advanced"] = "beginner"
    language: str = "zh-CN"
    top_k: int = Field(default=3, ge=1, le=8)


agent = TeacherAgent()
app = FastAPI(
    title="Gwent Teacher Agent",
    version="1.0.0",
    description="Read-only grounded explanation service for Strategy Core decisions.",
)


@app.get("/health")
def health() -> dict[str, str | bool]:
    return {"ok": True, "service": "gwent-teacher", "schema": TEACHER_RESPONSE_SCHEMA_VERSION}


@app.post("/v1/explain", response_model=ExplainResponse)
def explain(req: ExplainRequest) -> ExplainResponse:
    try:
        result = agent.explain(
            TeacherRequest(
                decision_packet=req.decision_packet,
                public_state=req.public_state,
                level=req.level,
                language=req.language,
                top_k=req.top_k,
            )
        )
    except (KeyError, TypeError, ValueError) as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    return ExplainResponse(response=result.to_dict())


@app.post("/v1/explain-turn", response_model=ExplainResponse)
def explain_turn(req: ExplainTurnRequest) -> ExplainResponse:
    try:
        result = agent.explain_turn(
            TeacherTurnRequest(
                turn_trace=req.turn_trace,
                level=req.level,
                language=req.language,
                top_k=req.top_k,
            )
        )
    except (KeyError, TypeError, ValueError) as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    return ExplainResponse(response=result.to_dict())
