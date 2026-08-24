from fastapi.testclient import TestClient

from services.teacher.api import app


def test_teacher_http_health_and_explain():
    client = TestClient(app)
    health = client.get("/health")
    assert health.status_code == 200
    assert health.json()["ok"] is True

    response = client.post(
        "/v1/explain",
        json={
            "decision_packet": {
                "decision_serial": 1,
                "actor_id": 0,
                "decision_kind": "play_or_pass",
                "recommended_option_index": 0,
                "state_value": 0.0,
                "candidates": [
                    {
                        "option_index": 0,
                        "probability": 1.0,
                        "option_kind": "pass",
                        "card_id": 0,
                    }
                ],
            },
            "level": "beginner",
        },
    )
    assert response.status_code == 200
    body = response.json()["response"]
    assert body["action_label"].startswith("PASS")
    assert body["policy_probability"] == 1.0
