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


def test_teacher_http_explains_a_current_turn_trace():
    client = TestClient(app)
    response = client.post(
        "/v1/explain-turn",
        json={
            "turn_trace": {
                "schema_version": "counterfactual-action-chain-v2",
                "boundary": "one_root_action_with_required_choices",
                "root": {"decision_serial": 1, "kind": "pass", "card_id": -1, "source_object_index": -1},
                "stopped_reason": "action_chain_resolved",
                "steps": [
                    {
                        "decision_serial": 1,
                        "parent_decision_serial": None,
                        "role": "root_action",
                        "actor_id": 0,
                        "index": 0,
                        "kind": "pass",
                        "card_id": -1,
                        "confidence": 1.0,
                        "value": 0.0,
                        "summary_before": {
                            "round": 1,
                            "p0": {"score": 0, "hand": 8},
                            "p1": {"score": 0, "hand": 8},
                        },
                    }
                ],
            }
        },
    )

    assert response.status_code == 200
    body = response.json()["response"]
    assert body["stopped_reason"] == "action_chain_resolved"
    assert len(body["steps"]) == 1
