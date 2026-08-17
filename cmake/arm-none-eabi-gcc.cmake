# CaDS Zero - CMake toolchain file for the ITSboard target (STM32F429ZI, Cortex-M4F)
#
# The Arm GNU toolchain shipped through vcpkg artifacts is picked up automatically
# when CADS_ARM_TOOLCHAIN_BIN is not supplied explicitly.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# --- locate the toolchain ----------------------------------------------------
if(NOT CADS_ARM_TOOLCHAIN_BIN)
    if(DEFINED ENV{CADS_ARM_TOOLCHAIN_BIN})
        set(CADS_ARM_TOOLCHAIN_BIN "$ENV{CADS_ARM_TOOLCHAIN_BIN}")
    else()
        file(GLOB _cads_gcc_candidates
             "$ENV{HOME}/.vcpkg/artifacts/*/compilers.arm.arm.none.eabi.gcc/*/bin")
        if(_cads_gcc_candidates)
            list(SORT _cads_gcc_candidates)
            list(GET _cads_gcc_candidates -1 CADS_ARM_TOOLCHAIN_BIN)
        endif()
    endif()
endif()

if(CADS_ARM_TOOLCHAIN_BIN)
    set(_cads_prefix "${CADS_ARM_TOOLCHAIN_BIN}/arm-none-eabi-")
else()
    set(_cads_prefix "arm-none-eabi-")   # rely on PATH
endif()

set(CMAKE_C_COMPILER   "${_cads_prefix}gcc")
set(CMAKE_CXX_COMPILER "${_cads_prefix}g++")
set(CMAKE_ASM_COMPILER "${_cads_prefix}gcc")
set(CMAKE_OBJCOPY      "${_cads_prefix}objcopy" CACHE FILEPATH "objcopy")
set(CMAKE_OBJDUMP      "${_cads_prefix}objdump" CACHE FILEPATH "objdump")
set(CMAKE_SIZE         "${_cads_prefix}size"    CACHE FILEPATH "size")
set(CMAKE_GDB          "${_cads_prefix}gdb"     CACHE FILEPATH "gdb")

# Do not try to run the produced binaries on the host during compiler checks.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# --- architecture flags ------------------------------------------------------
# STM32F429ZI: Cortex-M4 with single precision FPU (FPv4-SP-D16), hard float ABI.
set(CADS_ARCH_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")

set(CMAKE_C_FLAGS_INIT   "${CADS_ARCH_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${CADS_ARCH_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${CADS_ARCH_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${CADS_ARCH_FLAGS} --specs=nano.specs --specs=nosys.specs")
