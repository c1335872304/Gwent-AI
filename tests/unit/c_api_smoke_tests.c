#include "gwent/c/core.h"

#include <stdio.h>
#include <string.h>

static int expect(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAILED: %s\n", message);
        return 1;
    }
    return 0;
}

int main(void) {
    gwent_deck_a_match_config config = gwent_deck_a_default_config();
    config.seed = 123;
    config.starting_player_id = 0;
    config.invariant_policy = GWENT_C_INVARIANT_AFTER_APPLY;

    gwent_deck_a_game* game = gwent_deck_a_game_create(&config);
    if (expect(game != NULL, "game create")) {
        return 1;
    }

    size_t count = gwent_deck_a_game_legal_action_count(game);
    if (expect(count > 0, "initial legal action count")) {
        gwent_deck_a_game_destroy(game);
        return 1;
    }

    gwent_c_string actions = {0};
    if (expect(gwent_deck_a_game_legal_actions_json(game, &actions) == GWENT_C_OK, "legal actions json")) {
        gwent_deck_a_game_destroy(game);
        return 1;
    }
    if (expect(actions.data != NULL && actions.size >= 2 && actions.data[0] == '[', "legal actions json payload")) {
        gwent_c_string_free(&actions);
        gwent_deck_a_game_destroy(game);
        return 1;
    }
    gwent_c_string_free(&actions);

    gwent_c_action_status status = GWENT_C_ACTION_UNKNOWN;
    if (expect(gwent_deck_a_game_apply_legal_action_at(game, 0, &status) == GWENT_C_OK, "apply legal action at 0")) {
        gwent_deck_a_game_destroy(game);
        return 1;
    }
    if (expect(status == GWENT_C_ACTION_OK, "legal action status ok")) {
        gwent_deck_a_game_destroy(game);
        return 1;
    }

    gwent_c_string snapshot = {0};
    if (expect(gwent_deck_a_game_snapshot_json(game, &snapshot) == GWENT_C_OK, "snapshot json")) {
        gwent_deck_a_game_destroy(game);
        return 1;
    }
    if (expect(snapshot.data != NULL && strstr(snapshot.data, "\"players\"") != NULL, "snapshot contains players")) {
        gwent_c_string_free(&snapshot);
        gwent_deck_a_game_destroy(game);
        return 1;
    }
    gwent_c_string_free(&snapshot);

    gwent_c_string checksum = {0};
    if (expect(gwent_deck_a_game_checksum(game, &checksum) == GWENT_C_OK, "checksum")) {
        gwent_deck_a_game_destroy(game);
        return 1;
    }
    if (expect(checksum.data != NULL && checksum.size > 0, "checksum payload")) {
        gwent_c_string_free(&checksum);
        gwent_deck_a_game_destroy(game);
        return 1;
    }
    gwent_c_string_free(&checksum);

    if (expect(gwent_c_result_code_name(GWENT_C_OK) != NULL, "result name")) {
        gwent_deck_a_game_destroy(game);
        return 1;
    }
    if (expect(gwent_c_action_status_name(GWENT_C_ACTION_OK) != NULL, "status name")) {
        gwent_deck_a_game_destroy(game);
        return 1;
    }

    gwent_deck_a_game_destroy(game);
    return 0;
}
