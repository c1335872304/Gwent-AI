#!/usr/bin/env python3
"""Install the current build and verify that an external CMake consumer can use it."""

from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import textwrap


def run(cmd: list[str], *, cwd: pathlib.Path | None = None) -> None:
    subprocess.run(cmd, cwd=cwd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--work-dir", required=True)
    args = parser.parse_args()

    cmake = args.cmake
    build_dir = pathlib.Path(args.build_dir).resolve()
    work_dir = pathlib.Path(args.work_dir).resolve()
    install_dir = work_dir / "install"
    consumer_src = work_dir / "consumer"
    consumer_build = work_dir / "consumer-build"

    if work_dir.exists():
        shutil.rmtree(work_dir)
    consumer_src.mkdir(parents=True)

    run([cmake, "--install", str(build_dir), "--prefix", str(install_dir)])

    (consumer_src / "CMakeLists.txt").write_text(
        textwrap.dedent(
            """
            cmake_minimum_required(VERSION 3.20)
            project(gwent_package_consumer LANGUAGES CXX)
            find_package(gwent_cpp_core CONFIG REQUIRED)
            add_executable(consumer main.cpp)
            target_link_libraries(consumer PRIVATE gwent::core)
            target_compile_features(consumer PRIVATE cxx_std_20)
            """
        ).strip()
        + "\n",
        encoding="utf-8",
    )
    (consumer_src / "main.cpp").write_text(
        textwrap.dedent(
            """
            #include <gwent/api/core.hpp>
            #include <gwent/c/core.h>

            int main() {
                auto game = gwent::api::DeckAGame::create({});
                if (game.legal_actions().empty()) {
                    return 1;
                }

                gwent_deck_a_match_config config = gwent_deck_a_default_config();
                gwent_deck_a_game* c_game = gwent_deck_a_game_create(&config);
                if (c_game == nullptr) {
                    return 2;
                }
                const auto count = gwent_deck_a_game_legal_action_count(c_game);
                gwent_deck_a_game_destroy(c_game);
                if (count == 0) {
                    return 3;
                }

                gwent_rl_config rl_config = gwent_rl_default_config();
                gwent_rl_env* env = gwent_rl_env_create(&rl_config);
                if (env == nullptr) {
                    return 4;
                }
                const gwent_rl_observation* obs = gwent_rl_env_observation(env);
                const auto options = gwent_rl_env_option_count(env);
                gwent_rl_env_destroy(env);
                if (obs == nullptr || options == 0) {
                    return 5;
                }
                return 0;
            }
            """
        ).strip()
        + "\n",
        encoding="utf-8",
    )

    run([
        cmake,
        "-S",
        str(consumer_src),
        "-B",
        str(consumer_build),
        f"-DCMAKE_PREFIX_PATH={install_dir}",
        "-DCMAKE_BUILD_TYPE=Release",
    ])
    run([cmake, "--build", str(consumer_build), "--config", "Release"])
    executable = consumer_build / "consumer"
    if not executable.exists():
        executable = consumer_build / "Release" / "consumer.exe"
    run([str(executable)])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
