# Copyright 2026 Aditya Gudal
# SPDX-License-Identifier: Apache-2.0

add_executable(df_tests
    main.cpp
    test_clock.cpp
    test_chunk.cpp
    test_greedy_mesher.cpp
    test_weapon.cpp
    test_wave_director.cpp
    test_sim.cpp
)
target_link_libraries(df_tests PRIVATE df_game doctest::doctest)
driftfall_set_warnings(df_tests)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(df_tests)
