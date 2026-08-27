# Golden-image tests: run the simulator to a quiescent --screenshot and
# compare it, pixel for pixel, against a reference PNG in targets/sim/golden/.
#
# These are not Unity binaries - there is no C code under test here, only the
# simulator executable and a scene to put on its panel - so they do not use
# tests/unit's cads_add_unit_test(). ctest does not care what a test COMMAND
# is as long as it exits zero on success, so a Python script invoked the same
# way scripts/board_test.py already drives the real board fits the existing
# tooling shape better than inventing a C test runner for this alone.
#
# $<TARGET_FILE:cads-zero-sim> rather than a hardcoded path: it resolves to
# wherever this build actually put the executable, generator-expanded at
# test time, so out-of-source and multi-config builds do not need special
# casing here.

function(cads_add_golden_test name idle_ms)
    set(golden ${CADS_ROOT}/targets/sim/golden/${name}.png)
    add_test(
        NAME golden_${name}
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/golden_check.py compare
            --sim $<TARGET_FILE:cads-zero-sim>
            --golden ${golden}
            --diff ${CMAKE_CURRENT_BINARY_DIR}/${name}.diff.png
            --name ${name}
            --idle ${idle_ms}
    )
    # A screenshot run that never quiesces gives up on its own
    # (--screenshot-timeout, 10 s default); this is the backstop for a
    # simulator that hung outright rather than one that is merely slow.
    set_tests_properties(golden_${name} PROPERTIES TIMEOUT 60)
endfunction()

# Reachable with no console input at all - see targets/sim/golden/README.md
# for exactly how each scene is timed into view.
cads_add_golden_test(splash 250)
cads_add_golden_test(boot_desktop 2000)

# `cmake --build <build-dir> --target update_golden` (or `ninja update_golden`)
# regenerates every golden PNG from the current build - the review step for an
# intentional UI change. golden_check.py's own `update` subcommand is the same
# thing run by hand against one scene, for when only one changed.
add_custom_target(update_golden
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/golden_check.py update
        --sim $<TARGET_FILE:cads-zero-sim>
        --golden ${CADS_ROOT}/targets/sim/golden/splash.png
        --name splash --idle 250
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/golden_check.py update
        --sim $<TARGET_FILE:cads-zero-sim>
        --golden ${CADS_ROOT}/targets/sim/golden/boot_desktop.png
        --name boot_desktop --idle 2000
    DEPENDS cads-zero-sim
    COMMENT "Regenerating targets/sim/golden/*.png from the current build"
    VERBATIM
)
