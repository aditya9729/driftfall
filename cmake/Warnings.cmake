# Copyright 2026 Aditya Gudal
# SPDX-License-Identifier: Apache-2.0

function(driftfall_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(DRIFTFALL_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wdouble-promotion   # silent f32->f64 promotions are a real mobile perf tax
            # -Wnull-dereference is deliberately absent: GCC + libstdc++ fires
            # it from inside <vector>'s own inlined internals, so with -Werror
            # it fails the build on code we do not own.
        )
        if(DRIFTFALL_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
