from __future__ import annotations

from typing import Protocol


class TeacherTextProvider(Protocol):
    """Optional LLM/text provider boundary.

    Implementations receive a fully grounded prompt.  They are presentation
    providers only; the TeacherAgent keeps the selected action immutable.
    """

    def generate(self, prompt: str) -> str:
        ...
