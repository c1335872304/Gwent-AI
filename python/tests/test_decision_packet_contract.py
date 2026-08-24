from gwent_rl.contracts.decision_packet import (
    DECISION_PACKET_SCHEMA_VERSION,
    CandidateDecision,
    DecisionPacket,
)


def test_decision_packet_v0_is_serializable_without_runtime_integration():
    packet = DecisionPacket(
        decision_serial=7,
        actor_id=0,
        decision_kind="play_or_pass",
        recommended_option_index=2,
        state_value=0.25,
        candidates=(
            CandidateDecision(
                option_index=2,
                probability=0.8,
                option_kind="choose_insert_position",
                target_side_id=0,
                target_row_id=0,
                insert_position=2,
            ),
        ),
    )

    data = packet.to_dict()
    assert data["schema_version"] == DECISION_PACKET_SCHEMA_VERSION
    assert data["recommended_option_index"] == 2
    assert data["candidates"][0]["option_kind"] == "choose_insert_position"
    assert data["candidates"][0]["insert_position"] == 2
