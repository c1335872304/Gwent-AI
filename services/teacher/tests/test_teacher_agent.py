from services.teacher import TeacherAgent, TeacherRequest


def packet() -> dict:
    return {
        "schema_version": "decision-packet-v0",
        "decision_serial": 9,
        "actor_id": 0,
        "decision_kind": "play_or_pass",
        "recommended_option_index": 1,
        "state_value": 0.2,
        "candidates": [
            {
                "option_index": 1,
                "probability": 0.7,
                "option_kind": "play_card",
                "card_id": 202889,
                "target_row_id": 1,
                "insert_position": 0,
            },
            {
                "option_index": 0,
                "probability": 0.3,
                "option_kind": "pass",
                "card_id": 0,
            },
        ],
    }


def test_teacher_explains_selected_action_without_changing_it():
    result = TeacherAgent().explain(TeacherRequest(decision_packet=packet()))
    assert result.decision_serial == 9
    assert "暗影长者" in result.action_label
    assert result.policy_probability == 0.7
    assert result.alternatives[0].option_index == 0
    assert any("卡牌文本" in fact for fact in result.grounded_facts)
    assert any("不是神经网络内部思维过程" in caveat for caveat in result.caveats)


def test_teacher_accepts_trace_decision_shape():
    trace = {
        "decision_serial": 10,
        "actor_id": 1,
        "decision_kind": "choose_insert_position",
        "chosen_option_index": 3,
        "state_value": -0.1,
        "legal_options": [
            {
                "option_index": 3,
                "probability": 0.6,
                "option_kind": "choose_insert_position",
                "card_id": 0,
                "target_row_id": 0,
                "insert_position": 2,
            },
            {
                "option_index": 2,
                "probability": 0.4,
                "option_kind": "choose_insert_position",
                "card_id": 0,
                "target_row_id": 0,
                "insert_position": 1,
            },
        ],
    }
    result = TeacherAgent().explain(TeacherRequest(decision_packet=trace, level="advanced"))
    assert "第 3 个插入位置" in result.action_label
    assert result.prompt is not None


def test_teacher_rejects_packet_without_candidates():
    try:
        TeacherAgent().explain(TeacherRequest(decision_packet={"recommended_option_index": 0}))
    except ValueError as exc:
        assert "no candidates" in str(exc)
    else:
        raise AssertionError("expected ValueError")
