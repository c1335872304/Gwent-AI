cmake_minimum_required(VERSION 3.20)

project(gwent_cpp_core
    VERSION 0.1.0
    DESCRIPTION "Clean C++ rules core for a Gwent-like environment"
    LANGUAGES C CXX
)

option(GWENT_BUILD_TESTS "Build gwent-cpp-core tests" ON)
option(GWENT_BUILD_TRACE_TOOLS "Build gwent-cpp-core trace runner" ON)
set(GWENT_PYTHON_CORE "" CACHE PATH "Optional path to the extracted Python gwent core for Python/C++ golden pair tests")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(OpenMP)

option(GWENT_INSTALL_TRACE_TOOLS "Install gwent-cpp-core trace/profiling tools" ON)

add_library(gwent_core
    src/core/enums.cpp
    src/core/card_definition.cpp
    src/core/runtime_card.cpp
    src/core/state.cpp
    src/core/entity_index.cpp
    src/core/invariants.cpp
    src/game/setup.cpp
    src/game/card_catalog.cpp
    src/cards/supported_cards.cpp
    src/cards/deck_a.cpp
    src/generated/supported_card_data.cpp
    src/engine/action.cpp
    src/engine/action_status.cpp
    src/engine/decision.cpp
    src/engine/legal_actions.cpp
    src/engine/reducer.cpp
    src/engine/task.cpp
    src/engine/task_queue.cpp
    src/engine/effect_registry.cpp
    src/engine/resolution_frame.cpp
    src/engine/primitives.cpp
    src/engine/board_rules.cpp
    src/engine/row_effects.cpp
    src/engine/query.cpp
    src/engine/targets.cpp
    src/engine/primitive_effects.cpp
    src/engine/kernel_action_dispatch.cpp
    src/engine/kernel_trigger_dispatch.cpp
    src/engine/kernel_turn_flow.cpp
    src/engine/kernel_task_processor.cpp
    src/engine/kernel.cpp
    src/trace/state_snapshot.cpp
    src/api/core.cpp
    src/c/core_c.cpp
)

add_library(gwent::core ALIAS gwent_core)

target_compile_features(gwent_core PUBLIC cxx_std_20)
set_target_properties(gwent_core PROPERTIES
    OUTPUT_NAME gwent_core
    EXPORT_NAME core
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# MSVC does not export ordinary C++ symbols from DLLs unless they are explicitly
# marked with __declspec(dllexport).  The public C API already has export macros,
# but our C++ unit tests and trace tools also link against gwent::core.  Exporting
# all symbols on Windows keeps BUILD_SHARED_LIBS=ON usable for local development
# without adding DLL annotations to every C++ class/function.
if(WIN32 AND BUILD_SHARED_LIBS)
    set_target_properties(gwent_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

target_include_directories(gwent_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_definitions(gwent_core
    PUBLIC
        $<$<BOOL:${BUILD_SHARED_LIBS}>:GWENT_CORE_SHARED>
    PRIVATE
        $<$<BOOL:${BUILD_SHARED_LIBS}>:GWENT_CORE_BUILDING_LIBRARY>
        $<$<BOOL:${OpenMP_CXX_FOUND}>:GWENT_HAS_OPENMP=1>
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(gwent_core PRIVATE OpenMP::OpenMP_CXX)
endif()

target_compile_options(gwent_core PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)


if(GWENT_BUILD_TRACE_TOOLS)
    add_executable(gwent_trace_runner
        tools/trace_runner/trace_runner.cpp
    )
    target_link_libraries(gwent_trace_runner PRIVATE gwent::core)
    target_compile_features(gwent_trace_runner PRIVATE cxx_std_20)

    add_executable(gwent_random_game_smoke
        tools/fuzz/random_game_smoke.cpp
    )
    target_link_libraries(gwent_random_game_smoke PRIVATE gwent::core)
    target_compile_features(gwent_random_game_smoke PRIVATE cxx_std_20)

    add_executable(gwent_profile_smoke
        tools/profiling/profile_smoke.cpp
    )
    target_link_libraries(gwent_profile_smoke PRIVATE gwent::core)
    target_compile_features(gwent_profile_smoke PRIVATE cxx_std_20)
endif()

if(GWENT_BUILD_TESTS)
    enable_testing()

    add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

    # Unit tests intentionally use assert().  Release/RelWithDebInfo normally
    # define NDEBUG, which would compile those checks out and produce false
    # green ctest runs.  Keep assertions live for every test target.
    add_compile_options(
        $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UNDEBUG>
    )

    add_executable(gwent_core_model_tests
        tests/unit/core_model_tests.cpp
    )
    target_link_libraries(gwent_core_model_tests PRIVATE gwent::core)
    target_compile_features(gwent_core_model_tests PRIVATE cxx_std_20)

    add_executable(gwent_setup_tests
        tests/unit/setup_tests.cpp
    )
    target_link_libraries(gwent_setup_tests PRIVATE gwent::core)
    target_compile_features(gwent_setup_tests PRIVATE cxx_std_20)

    add_executable(gwent_entity_index_query_tests
        tests/unit/entity_index_query_tests.cpp
    )
    target_link_libraries(gwent_entity_index_query_tests PRIVATE gwent::core)
    target_compile_features(gwent_entity_index_query_tests PRIVATE cxx_std_20)

    add_executable(gwent_legal_actions_tests
        tests/unit/legal_actions_tests.cpp
    )
    target_link_libraries(gwent_legal_actions_tests PRIVATE gwent::core)
    target_compile_features(gwent_legal_actions_tests PRIVATE cxx_std_20)

    add_test(NAME gwent_core_model_tests COMMAND gwent_core_model_tests)
    add_test(NAME gwent_setup_tests COMMAND gwent_setup_tests)
    add_test(NAME gwent_entity_index_query_tests COMMAND gwent_entity_index_query_tests)
    add_executable(gwent_reducer_tests
        tests/unit/reducer_tests.cpp
    )
    target_link_libraries(gwent_reducer_tests PRIVATE gwent::core)
    target_compile_features(gwent_reducer_tests PRIVATE cxx_std_20)
    target_compile_definitions(gwent_reducer_tests PRIVATE GWENT_SUPPRESS_LEGACY_REDUCER_DEPRECATION=1)

    add_test(NAME gwent_legal_actions_tests COMMAND gwent_legal_actions_tests)
    add_test(NAME gwent_reducer_tests COMMAND gwent_reducer_tests)

    add_executable(gwent_kernel_tests
        tests/unit/kernel_tests.cpp
    )
    target_link_libraries(gwent_kernel_tests PRIVATE gwent::core)
    target_compile_features(gwent_kernel_tests PRIVATE cxx_std_20)
    add_test(NAME gwent_kernel_tests COMMAND gwent_kernel_tests)

    add_executable(gwent_targets_primitive_effects_tests
        tests/unit/targets_primitive_effects_tests.cpp
    )
    target_link_libraries(gwent_targets_primitive_effects_tests PRIVATE gwent::core)
    target_compile_features(gwent_targets_primitive_effects_tests PRIVATE cxx_std_20)
    add_test(NAME gwent_targets_primitive_effects_tests COMMAND gwent_targets_primitive_effects_tests)

    add_executable(gwent_deck_a_basic_effects_tests
        tests/unit/deck_a_basic_effects_tests.cpp
    )
    target_link_libraries(gwent_deck_a_basic_effects_tests PRIVATE gwent::core)
    target_compile_features(gwent_deck_a_basic_effects_tests PRIVATE cxx_std_20)
    add_test(NAME gwent_deck_a_basic_effects_tests COMMAND gwent_deck_a_basic_effects_tests)

    add_executable(gwent_pending_decision_tests
        tests/unit/pending_decision_tests.cpp
    )
    target_link_libraries(gwent_pending_decision_tests PRIVATE gwent::core)
    target_compile_features(gwent_pending_decision_tests PRIVATE cxx_std_20)
    add_test(NAME gwent_pending_decision_tests COMMAND gwent_pending_decision_tests)

    add_executable(gwent_listener_trigger_tests
        tests/unit/listener_trigger_tests.cpp
    )
    target_link_libraries(gwent_listener_trigger_tests PRIVATE gwent::core)
    target_compile_features(gwent_listener_trigger_tests PRIVATE cxx_std_20)
    add_test(NAME gwent_listener_trigger_tests COMMAND gwent_listener_trigger_tests)

    add_executable(gwent_status_round_transition_tests
        tests/unit/status_round_transition_tests.cpp
    )
    target_link_libraries(gwent_status_round_transition_tests PRIVATE gwent::core)
    target_compile_features(gwent_status_round_transition_tests PRIVATE cxx_std_20)
    add_test(NAME gwent_status_round_transition_tests COMMAND gwent_status_round_transition_tests)

    add_executable(gwent_deck_a_catalog_effects_tests
        tests/unit/deck_a_catalog_effects_tests.cpp
    )
    target_link_libraries(gwent_deck_a_catalog_effects_tests PRIVATE gwent::core)
    target_compile_features(gwent_deck_a_catalog_effects_tests PRIVATE cxx_std_20)
    add_test(NAME gwent_deck_a_catalog_effects_tests COMMAND gwent_deck_a_catalog_effects_tests)

    add_executable(gwent_special_primitives_tests
        tests/unit/special_primitives_tests.cpp
    )
    target_link_libraries(gwent_special_primitives_tests PRIVATE gwent::core)
    target_compile_features(gwent_special_primitives_tests PRIVATE cxx_std_20)
    add_test(NAME gwent_special_primitives_tests COMMAND gwent_special_primitives_tests)

    add_executable(gwent_order_end_turn_regis_tests
        tests/unit/order_end_turn_regis_tests.cpp
    )
    target_link_libraries(gwent_order_end_turn_regis_tests PRIVATE gwent::core)
    target_compile_features(gwent_order_end_turn_regis_tests PRIVATE cxx_std_20)
    add_test(NAME gwent_order_end_turn_regis_tests COMMAND gwent_order_end_turn_regis_tests)

    add_executable(gwent_deck_a_deck_play_tests
        tests/unit/deck_a_deck_play_tests.cpp
    )
    target_link_libraries(gwent_deck_a_deck_play_tests PRIVATE gwent::core)
    target_compile_features(gwent_deck_a_deck_play_tests PRIVATE cxx_std_20)
    add_test(NAME gwent_deck_a_deck_play_tests COMMAND gwent_deck_a_deck_play_tests)

    add_executable(gwent_deck_a_m23_parity_tests
        tests/unit/deck_a_m23_parity_tests.cpp
    )
    target_link_libraries(gwent_deck_a_m23_parity_tests PRIVATE gwent::core)
    target_compile_features(gwent_deck_a_m23_parity_tests PRIVATE cxx_std_20)
    add_test(NAME gwent_deck_a_m23_parity_tests COMMAND gwent_deck_a_m23_parity_tests)


    add_executable(gwent_invariant_api_tests
        tests/unit/invariant_api_tests.cpp
    )
    target_link_libraries(gwent_invariant_api_tests PRIVATE gwent::core)
    target_compile_features(gwent_invariant_api_tests PRIVATE cxx_std_20)
    add_test(NAME gwent_invariant_api_tests COMMAND gwent_invariant_api_tests)

    add_executable(gwent_generic_game_api_tests
        tests/unit/generic_game_api_tests.cpp
    )
    target_link_libraries(gwent_generic_game_api_tests PRIVATE gwent::core)
    target_compile_features(gwent_generic_game_api_tests PRIVATE cxx_std_20)
    add_test(NAME gwent_generic_game_api_tests COMMAND gwent_generic_game_api_tests)

    add_executable(gwent_trace_snapshot_tests
        tests/unit/trace_snapshot_tests.cpp
    )
    target_link_libraries(gwent_trace_snapshot_tests PRIVATE gwent::core)
    target_compile_features(gwent_trace_snapshot_tests PRIVATE cxx_std_20)
    add_test(NAME gwent_trace_snapshot_tests COMMAND gwent_trace_snapshot_tests)

    add_executable(gwent_performance_metrics_tests
        tests/unit/performance_metrics_tests.cpp
    )
    target_link_libraries(gwent_performance_metrics_tests PRIVATE gwent::core)
    target_compile_features(gwent_performance_metrics_tests PRIVATE cxx_std_20)
    add_test(NAME gwent_performance_metrics_tests COMMAND gwent_performance_metrics_tests)

    add_executable(gwent_setup_golden_parity_tests
        tests/unit/setup_golden_parity_tests.cpp
    )
    target_link_libraries(gwent_setup_golden_parity_tests PRIVATE gwent::core)
    target_compile_features(gwent_setup_golden_parity_tests PRIVATE cxx_std_20)
    add_test(NAME gwent_setup_golden_parity_tests COMMAND gwent_setup_golden_parity_tests)

    add_executable(gwent_v6_optimization_tests
        tests/unit/v6_optimization_tests.cpp
    )
    target_link_libraries(gwent_v6_optimization_tests PRIVATE gwent::core)
    target_compile_features(gwent_v6_optimization_tests PRIVATE cxx_std_20)
    add_test(NAME gwent_v6_optimization_tests COMMAND gwent_v6_optimization_tests)

    add_executable(gwent_v7_optimization_tests
        tests/unit/v7_optimization_tests.cpp
    )
    target_link_libraries(gwent_v7_optimization_tests PRIVATE gwent::core)
    target_compile_features(gwent_v7_optimization_tests PRIVATE cxx_std_20)
    add_test(NAME gwent_v7_optimization_tests COMMAND gwent_v7_optimization_tests)

    add_executable(gwent_v8_finalization_tests
        tests/unit/v8_finalization_tests.cpp
    )
    target_link_libraries(gwent_v8_finalization_tests PRIVATE gwent::core)
    target_compile_features(gwent_v8_finalization_tests PRIVATE cxx_std_20)
    target_compile_definitions(gwent_v8_finalization_tests PRIVATE GWENT_SUPPRESS_LEGACY_REDUCER_DEPRECATION=1)
    add_test(NAME gwent_v8_finalization_tests COMMAND gwent_v8_finalization_tests)

    add_executable(gwent_strategic_reward_tests
        tests/unit/strategic_reward_tests.cpp
    )
    target_link_libraries(gwent_strategic_reward_tests PRIVATE gwent::core)
    target_compile_features(gwent_strategic_reward_tests PRIVATE cxx_std_20)
    add_test(NAME gwent_strategic_reward_tests COMMAND gwent_strategic_reward_tests)

    add_executable(gwent_card_data_generation_tests
        tests/unit/card_data_generation_tests.cpp
    )
    target_link_libraries(gwent_card_data_generation_tests PRIVATE gwent::core)
    target_compile_features(gwent_card_data_generation_tests PRIVATE cxx_std_20)
    add_test(NAME gwent_card_data_generation_tests COMMAND gwent_card_data_generation_tests)

    add_executable(gwent_c_api_smoke_tests
        tests/unit/c_api_smoke_tests.c
    )
    target_link_libraries(gwent_c_api_smoke_tests PRIVATE gwent::core)
    add_test(NAME gwent_c_api_smoke_tests COMMAND gwent_c_api_smoke_tests)

    add_executable(gwent_rl_c_api_tests
        tests/unit/rl_c_api_tests.c
    )
    target_link_libraries(gwent_rl_c_api_tests PRIVATE gwent::core)
    add_test(NAME gwent_rl_c_api_tests COMMAND gwent_rl_c_api_tests)

    add_executable(gwent_rl_collector_c_api_tests
        tests/unit/rl_collector_c_api_tests.c
    )
    target_link_libraries(gwent_rl_collector_c_api_tests PRIVATE gwent::core)
    add_test(NAME gwent_rl_collector_c_api_tests COMMAND gwent_rl_collector_c_api_tests)

    if(GWENT_BUILD_TRACE_TOOLS)
        add_test(
            NAME gwent_random_game_smoke
            COMMAND $<TARGET_FILE:gwent_random_game_smoke>
                --games 1000
                --seed 424242
                --max-actions 512
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )

        add_test(
            NAME gwent_profile_smoke
            COMMAND $<TARGET_FILE:gwent_profile_smoke>
                --games 64
                --seed 2026041
                --max-actions 512
                --json
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )

        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            add_test(
                NAME gwent_card_data_codegen_check
                COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tools/codegen/generate_card_data.py
                    --check
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            )

            add_test(
                NAME gwent_cmake_package_smoke
                COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tools/packaging/run_cmake_package_smoke.py
                    --cmake ${CMAKE_COMMAND}
                    --build-dir ${CMAKE_CURRENT_BINARY_DIR}
                    --work-dir ${CMAKE_CURRENT_BINARY_DIR}/package_smoke
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            )

            if(BUILD_SHARED_LIBS)
                add_test(
                    NAME gwent_python_ctypes_collector_smoke
                    COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/python/tests/smoke_ctypes_collector.py
                        --num-envs 16
                        --rounds 8
                        --library $<TARGET_FILE:gwent_core>
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                )
                set_tests_properties(gwent_python_ctypes_collector_smoke PROPERTIES
                    ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python/src"
                )

                add_test(
                    NAME gwent_python_training_loop_smoke
                    COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/python/tests/smoke_training_loop.py
                        --library $<TARGET_FILE:gwent_core>
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                )
                set_tests_properties(gwent_python_training_loop_smoke PROPERTIES
                    ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python/src"
                )
            endif()

            add_test(
                NAME gwent_python_benchmark_analysis_smoke
                COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/python/tests/smoke_benchmark_analysis.py
                    --work-dir ${CMAKE_CURRENT_BINARY_DIR}/benchmark_analysis_smoke
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            )
            set_tests_properties(gwent_python_benchmark_analysis_smoke PROPERTIES
                ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python/src"
            )

            add_test(
                NAME gwent_cpp_golden_cases
                COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tools/golden/run_cpp_golden_cases.py
                    --runner $<TARGET_FILE:gwent_trace_runner>
                    --out-dir ${CMAKE_CURRENT_BINARY_DIR}/golden/cpp
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            )

            add_test(
                NAME gwent_trace_protocol_smoke
                COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tools/golden/run_trace_protocol_smoke.py
                    --runner $<TARGET_FILE:gwent_trace_runner>
                    --out-dir ${CMAKE_CURRENT_BINARY_DIR}/golden/trace_protocol
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            )


            if(GWENT_PYTHON_CORE)
                add_test(
                    NAME gwent_python_cpp_setup_pair
                    COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/golden/run_python_cpp_setup_pair.py
                        --cpp-runner $<TARGET_FILE:gwent_trace_runner>
                        --python-core ${GWENT_PYTHON_CORE}
                        --out-dir ${CMAKE_CURRENT_BINARY_DIR}/golden/python_cpp
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                )
                add_test(
                    NAME gwent_python_cpp_semantic_pair
                    COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/golden/run_python_cpp_semantic_pair.py
                        --cpp-runner $<TARGET_FILE:gwent_trace_runner>
                        --python-core ${GWENT_PYTHON_CORE}
                        --out-dir ${CMAKE_CURRENT_BINARY_DIR}/golden/python_cpp_semantic
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                )
                add_test(
                    NAME gwent_deck_a_full_parity_audit
                    COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/golden/run_deck_a_full_parity_audit.py
                        --cpp-runner $<TARGET_FILE:gwent_trace_runner>
                        --python-core ${GWENT_PYTHON_CORE}
                        --out-dir ${CMAKE_CURRENT_BINARY_DIR}/golden/deck_a_full_parity
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                )
                add_test(
                    NAME gwent_legal_actions_parity_audit
                    COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/golden/run_legal_actions_parity_audit.py
                        --cpp-runner $<TARGET_FILE:gwent_trace_runner>
                        --python-core ${GWENT_PYTHON_CORE}
                        --out-dir ${CMAKE_CURRENT_BINARY_DIR}/golden/legal_actions_parity
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                )
            endif()
        endif()
    endif()
endif()


write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/gwent_cpp_coreConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gwent_cpp_coreConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/gwent_cpp_coreConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gwent_cpp_core
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gwent_cpp_core.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/gwent_cpp_core.pc
    @ONLY
)

install(TARGETS gwent_core
    EXPORT gwent_cpp_coreTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(GWENT_BUILD_TRACE_TOOLS AND GWENT_INSTALL_TRACE_TOOLS)
    install(TARGETS gwent_trace_runner gwent_random_game_smoke gwent_profile_smoke
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY data/cards/ DESTINATION ${CMAKE_INSTALL_DATADIR}/gwent_cpp_core/cards)
install(DIRECTORY data/decks/ DESTINATION ${CMAKE_INSTALL_DATADIR}/gwent_cpp_core/decks)
install(DIRECTORY data/embeddings/ DESTINATION ${CMAKE_INSTALL_DATADIR}/gwent_cpp_core/embeddings)
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES
    tools/golden/TRACE_SCHEMA.md
    tools/golden/ACTION_SCRIPT_SCHEMA.md
    tools/golden/LEGAL_SURFACE_SCHEMA.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/schemas
)

install(EXPORT gwent_cpp_coreTargets
    NAMESPACE gwent::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gwent_cpp_core
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/gwent_cpp_coreConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/gwent_cpp_coreConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gwent_cpp_core
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/gwent_cpp_core.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
