from __future__ import annotations

from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from app.api.game import router as game_router
from app.api.teacher import router as teacher_router
from app.clients.gwent_core import GwentCoreClient
from app.clients.teacher import TeacherClient
from app.config import get_settings
from app.services.game_service import GameService
from app.services.teacher_preview_service import TeacherPreviewService

settings = get_settings()


@asynccontextmanager
async def lifespan(app: FastAPI):
    core = GwentCoreClient(settings.core_api_url, timeout_s=settings.request_timeout_s)
    teacher = TeacherClient(settings.teacher_api_url, timeout_s=settings.teacher_timeout_s)
    await core.start()
    await teacher.start()
    game = GameService(core)
    app.state.game_service = game
    app.state.teacher_client = teacher
    app.state.teacher_preview_service = TeacherPreviewService(game, teacher)
    try:
        yield
    finally:
        await teacher.aclose()
        await core.aclose()


app = FastAPI(
    title=settings.app_name,
    version="1.0.0",
    description="Typed BFF for the Gwent AI interactive demo.",
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.cors_origin_list,
    allow_credentials=False,
    allow_methods=["GET", "POST", "OPTIONS"],
    allow_headers=["Content-Type"],
)

app.include_router(game_router)
app.include_router(teacher_router)


@app.get("/")
def root() -> dict[str, str]:
    return {
        "service": "gwent-app-api",
        "docs": "/docs",
        "health": "/api/health",
    }


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(
        "app.main:app",
        host="127.0.0.1",
        port=8010,
        reload=True,
    )
