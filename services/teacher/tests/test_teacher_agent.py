from services.teacher import TeacherAgent, TeacherRequest, TeacherTurnRequest


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


def test_teacher_explains_core_produced_current_turn_trace() -> None:
    trace = {
        "schema_version": "counterfactual-action-chain-v2",
        "boundary": "one_root_action_with_required_choices",
        "root": {"decision_serial": 1, "kind": "play_card", "card_id": 202889, "source_object_index": 3},
        "stopped_reason": "action_chain_resolved",
        "steps": [
            {
                "decision_serial": 1,
                "parent_decision_serial": None,
                "role": "root_action",
                "actor_id": 0,
                "index": 2,
                "kind": "play_card",
                "card_id": 202889,
                "source_object_index": 3,
                "target_object_index": -1,
                "target_side": -1,
                "target_zone": -1,
                "target_row": -1,
                "insert_position": -1,
                "confidence": 0.62,
                "value": 0.11,
                "summary_before": {
                    "round": 1,
                    "p0": {"score": 0, "hand": 10},
                    "p1": {"score": 0, "hand": 10},
                },
            },
            {
                "decision_serial": 2,
                "parent_decision_serial": 1,
                "role": "required_choice",
                "actor_id": 0,
                "index": 0,
                "kind": "choose_row",
                "card_id": 202889,
                "source_object_index": -1,
                "target_object_index": -1,
                "target_side": -1,
                "target_zone": -1,
                "target_row": -1,
                "insert_position": -1,
                "confidence": 0.91,
                "value": 0.11,
                "summary_before": {
                    "round": 1,
                    "p0": {"score": 0, "hand": 10},
                    "p1": {"score": 0, "hand": 10},
                },
            },
        ],
    }

    result = TeacherAgent().explain_turn(TeacherTurnRequest(turn_trace=trace))

    assert result.headline.startswith("如果由 AI 接管")
    assert result.stopped_reason == "action_chain_resolved"
    assert len(result.steps) == 2
    assert result.steps[0].decision_serial == 1
    assert result.steps[1].action_role == "required_choice"
    assert "只读行动链推演" in result.caveats[0]


def test_teacher_keeps_leader_target_choice_context() -> None:
    trace = {
        "schema_version": "counterfactual-action-chain-v2",
        "boundary": "one_root_action_with_required_choices",
        "root": {"decision_serial": 4, "kind": "use_leader", "card_id": 202185, "source_object_index": 60},
        "stopped_reason": "action_chain_resolved",
        "steps": [
            {
                "decision_serial": 4,
                "parent_decision_serial": None,
                "role": "root_action",
                "actor_id": 0,
                "index": 0,
                "kind": "use_leader",
                "card_id": 202185,
                "source_card_id": 202185,
                "target_card_id": -1,
                "source_zone": 7,
                "source_object_index": 60,
                "target_object_index": -1,
                "target_side": -1,
                "target_zone": -1,
                "target_row": -1,
                "insert_position": -1,
                "confidence": 1.0,
                "value": 0.01,
                "summary_before": {"round": 1, "p0": {"score": 4, "hand": 9}, "p1": {"score": 10, "hand": 9}},
            },
            {
                "decision_serial": 5,
                "parent_decision_serial": 4,
                "role": "required_choice",
                "actor_id": 0,
                "index": 0,
                "kind": "choose_card",
                "card_id": 202889,
                "source_card_id": 202185,
                "target_card_id": 202889,
                "source_zone": 7,
                "source_object_index": 60,
                "target_object_index": 61,
                "target_side": 1,
                "target_zone": 4,
                "target_row": 1,
                "insert_position": -1,
                "confidence": 1.0,
                "value": 0.01,
                "summary_before": {
                    "round": 1,
                    "p0": {"score": 4, "hand": 9},
                    "p1": {"score": 10, "hand": 9},
                },
            }
        ],
    }

    result = TeacherAgent().explain_turn(TeacherTurnRequest(turn_trace=trace))

    assert result.steps[1].action_label == "使用领袖《腥膻之味》选择敌方《暗影长者》"
    assert result.steps[1].parent_decision_serial == 4
