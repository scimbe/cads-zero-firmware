# Host unit tests for the storage module. Always built against
# src/cads_flash_host.c (see the parent CMakeLists.txt's target selection) -
# these tests never touch real hardware and never run there.

if(NOT EXISTS ${CADS_ROOT}/lib/Unity/src/unity.c)
    message(FATAL_ERROR "lib/Unity is empty. Run: git submodule update --init lib/Unity")
endif()

if(NOT TARGET cads_unity)
    add_library(cads_unity STATIC ${CADS_ROOT}/lib/Unity/src/unity.c)
    target_include_directories(cads_unity PUBLIC ${CADS_ROOT}/lib/Unity/src)
endif()

function(cads_storage_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE cads_storage cads_unity cads_flags)
    add_test(NAME ${name} COMMAND ${name})
    # A unit test that runs for a minute is a hung unit test.
    set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()

cads_storage_add_test(test_flash test_flash.c)
cads_storage_add_test(test_storage test_storage.c)
cads_storage_add_test(test_kv test_kv.c)
