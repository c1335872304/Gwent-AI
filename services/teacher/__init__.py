"""Grounded Teacher Agent runtime.

The Teacher Agent is a read-only explanation consumer.  It never chooses or
changes legal actions and never participates in PPO training.
"""

from .agent import TeacherAgent
from .models import TeacherRequest, TeacherResponse, TeacherTurnRequest, TeacherTurnResponse

__all__ = ["TeacherAgent", "TeacherRequest", "TeacherResponse", "TeacherTurnRequest", "TeacherTurnResponse"]
