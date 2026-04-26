# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.20)
project(rp4 C CXX ASM)

# ─── rp4 ────────────────────────────────────────────────────────────────────
#
# User-space resource managers for qnx-micro on Raspberry Pi 4 (BCM2711).
# Bare-metal aarch64. Links against pre-built kernel from ~/qnx-micro.
#
# Host build (test/verify on macOS):
#   cmake -S . -B build \
#     -DCMAKE_C_COMPILER=$(brew --prefix llvm)/bin/clang \
#     -DCMAKE_CXX_COMPILER=$(brew --prefix llvm)/bin/clang++ \
#     -DRP4_HOST_BUILD=ON
#
# Cross build (bare-metal RPi4):
#   ./scripts/cross-build.sh
#
# ─────────────────────────────────────────────────────────────────────────────

option(RP4_HOST_BUILD "Build for host (macOS/Linux) testing, not bare-metal" ON)

# ─── Compiler gate ───────────────────────────────────────────────────────────

if(CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    message(FATAL_ERROR
        "AppleClang detected. Use upstream LLVM Clang.\n"
        "  macOS:  brew install llvm\n"
        "  Then: -DCMAKE_C_COMPILER=$(brew --prefix llvm)/bin/clang")
elseif(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Non-Clang compiler: ${CMAKE_C_COMPILER_ID}")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─── Generate config.db (single source of truth for tool paths) ─────────────

find_program(DOTNET dotnet)
if(NOT DOTNET)
    message(FATAL_ERROR
        "dotnet not found. Install .NET SDK:\n"
        "  macOS:  brew install dotnet\n"
        "  Ubuntu: apt install dotnet-sdk-10.0\n"
        "  Or: https://dot.net/download")
endif()

set(CONFIG_DB "${CMAKE_SOURCE_DIR}/config.db")
if(NOT EXISTS "${CONFIG_DB}" OR
   "${CMAKE_SOURCE_DIR}/scripts/seed_config.fsx" IS_NEWER_THAN "${CONFIG_DB}")
    message(STATUS "Generating config.db...")
    execute_process(
        COMMAND ${DOTNET} fsi ${CMAKE_SOURCE_DIR}/scripts/seed_config.fsx -- ${CONFIG_DB}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE SEED_RESULT)
    if(NOT SEED_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to generate config.db")
    endif()
endif()

# ─── Generate C++26 config module from config.db ────────────────────────────

set(CONFIG_CPPM "${CMAKE_SOURCE_DIR}/src/config/config.cppm")
if(NOT EXISTS "${CONFIG_CPPM}" OR
   "${CONFIG_DB}" IS_NEWER_THAN "${CONFIG_CPPM}")
    message(STATUS "Generating config.cppm from config.db...")
    execute_process(
        COMMAND ${DOTNET} fsi ${CMAKE_SOURCE_DIR}/scripts/gen_config_module.fsx -- ${CONFIG_DB} ${CONFIG_CPPM}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE GEN_RESULT)
    if(NOT GEN_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to generate config.cppm")
    endif()
endif()

# ─── Kernel link (read-only submodule at lib/qnx-micro) ─────────────────────

set(KERNEL_DIR "${CMAKE_SOURCE_DIR}/lib/qnx-micro")
set(KERNEL_BUILD "${KERNEL_DIR}/build")

if(NOT EXISTS "${KERNEL_BUILD}/qnx.kernel.pcm")
    message(FATAL_ERROR
        "Kernel PCMs not found at ${KERNEL_BUILD}/.\n"
        "  Build the kernel first:\n"
        "    cd lib/qnx-micro\n"
        "    cmake -S . -B build -DCMAKE_C_COMPILER=$(brew --prefix llvm)/bin/clang -DCMAKE_CXX_COMPILER=$(brew --prefix llvm)/bin/clang++\n"
        "    cmake --build build")
endif()

set(KERNEL_PCMS
    ${KERNEL_BUILD}/std.pcm
    ${KERNEL_BUILD}/qnx.types.pcm
    ${KERNEL_BUILD}/qnx.scheduler.pcm
    ${KERNEL_BUILD}/qnx.ipc.pcm
    ${KERNEL_BUILD}/qnx.memory.pcm
    ${KERNEL_BUILD}/qnx.driver.pcm
    ${KERNEL_BUILD}/qnx.pps.pcm
    ${KERNEL_BUILD}/qnx.kernel.pcm)

set(KERNEL_OBJS
    ${KERNEL_BUILD}/qnx_types.o
    ${KERNEL_BUILD}/qnx_scheduler.o
    ${KERNEL_BUILD}/qnx_ipc.o
    ${KERNEL_BUILD}/qnx_memory.o
    ${KERNEL_BUILD}/qnx_driver.o
    ${KERNEL_BUILD}/qnx_pps.o
    ${KERNEL_BUILD}/qnx_kernel.o)

# Module flags
set(KERNEL_MOD_FLAGS "")
foreach(pcm ${KERNEL_PCMS})
    get_filename_component(pcm_file ${pcm} NAME)
    string(REGEX REPLACE "\\.pcm$" "" mod_name "${pcm_file}")
    list(APPEND KERNEL_MOD_FLAGS -fmodule-file=${mod_name}=${pcm})
endforeach()

set(CXX ${CMAKE_CXX_COMPILER})
set(MODFLAGS -std=c++26 -stdlib=libc++ -Wno-reserved-module-identifier)

# ─── rp4.config module (generated from config.db) ───────────────────────────

set(CFG_PCM ${CMAKE_BINARY_DIR}/rp4.config.pcm)
set(CFG_OBJ ${CMAKE_BINARY_DIR}/rp4_config.o)

add_custom_command(OUTPUT ${CFG_PCM}
    COMMAND ${CXX} ${MODFLAGS} ${KERNEL_MOD_FLAGS}
            --precompile ${CMAKE_SOURCE_DIR}/src/config/config.cppm -o ${CFG_PCM}
    DEPENDS ${KERNEL_PCMS} ${CMAKE_SOURCE_DIR}/src/config/config.cppm
    COMMENT "Precompiling rp4.config")

add_custom_command(OUTPUT ${CFG_OBJ}
    COMMAND ${CXX} ${MODFLAGS} ${KERNEL_MOD_FLAGS}
            -fmodule-file=rp4.config=${CFG_PCM}
            -c ${CFG_PCM} -o ${CFG_OBJ}
    DEPENDS ${CFG_PCM}
    COMMENT "Compiling rp4.config")

# ─── rp4.hal module ─────────────────────────────────────────────────────────

set(HAL_SRC ${CMAKE_SOURCE_DIR}/src/hal)
set(HAL_PCM ${CMAKE_BINARY_DIR}/rp4.hal.pcm)
set(HAL_OBJ ${CMAKE_BINARY_DIR}/rp4_hal.o)

add_custom_command(OUTPUT ${HAL_PCM}
    COMMAND ${CXX} ${MODFLAGS} ${KERNEL_MOD_FLAGS}
            --precompile ${HAL_SRC}/hal.cppm -o ${HAL_PCM}
    DEPENDS ${KERNEL_PCMS} ${HAL_SRC}/hal.cppm
    COMMENT "Precompiling rp4.hal")

add_custom_command(OUTPUT ${HAL_OBJ}
    COMMAND ${CXX} ${MODFLAGS} ${KERNEL_MOD_FLAGS}
            -fmodule-file=rp4.hal=${HAL_PCM}
            -c ${HAL_PCM} -o ${HAL_OBJ}
    DEPENDS ${HAL_PCM}
    COMMENT "Compiling rp4.hal")

# ─── rp4.proc module ────────────────────────────────────────────────────────

set(PROC_PCM ${CMAKE_BINARY_DIR}/rp4.proc.pcm)
set(PROC_OBJ ${CMAKE_BINARY_DIR}/rp4_proc.o)

add_custom_command(OUTPUT ${PROC_PCM}
    COMMAND ${CXX} ${MODFLAGS} ${KERNEL_MOD_FLAGS}
            --precompile ${CMAKE_SOURCE_DIR}/src/proc/proc.cppm -o ${PROC_PCM}
    DEPENDS ${KERNEL_PCMS} ${CMAKE_SOURCE_DIR}/src/proc/proc.cppm
    COMMENT "Precompiling rp4.proc")

add_custom_command(OUTPUT ${PROC_OBJ}
    COMMAND ${CXX} ${MODFLAGS} ${KERNEL_MOD_FLAGS}
            -fmodule-file=rp4.proc=${PROC_PCM}
            -c ${PROC_PCM} -o ${PROC_OBJ}
    DEPENDS ${PROC_PCM}
    COMMENT "Compiling rp4.proc")

# ─── All module flags for apps ──────────────────────────────────────────────

set(APP_MOD_FLAGS ${KERNEL_MOD_FLAGS}
    -fmodule-file=rp4.config=${CFG_PCM}
    -fmodule-file=rp4.hal=${HAL_PCM}
    -fmodule-file=rp4.proc=${PROC_PCM})

set(ALL_OBJS ${CFG_OBJ} ${HAL_OBJ} ${PROC_OBJ} ${KERNEL_OBJS})

# ─── Apps (auto-discover src/apps/*.cpp) ────────────────────────────────────
# Host build: each app has main(). Compile + link as standalone binary.
# Drop a .cpp in src/apps/ → CLion picks it up. Zero CMake changes.

file(GLOB APP_SOURCES ${CMAKE_SOURCE_DIR}/src/apps/*.cpp)
foreach(APP_SRC ${APP_SOURCES})
    get_filename_component(APP_NAME ${APP_SRC} NAME_WE)

    # Skip apps that need special linking: configsrv (Poco), storagemgr (FatFs+dr_libs), shell (links all)
    if(APP_NAME STREQUAL "configsrv" OR APP_NAME STREQUAL "shell" OR APP_NAME STREQUAL "storagemgr")
        continue()
    endif()

    # Per-app include paths for vendor C headers
    set(APP_EXTRA_FLAGS "")
    if(APP_NAME STREQUAL "storagemgr")
        set(APP_EXTRA_FLAGS
            -isystem ${CMAKE_SOURCE_DIR}/vendor/fatfs/source
            -isystem ${CMAKE_SOURCE_DIR}/vendor/dr_libs)
    endif()

    set(APP_OBJ ${CMAKE_BINARY_DIR}/apps/${APP_NAME}.o)
    set(APP_BIN ${CMAKE_BINARY_DIR}/${APP_NAME})

    set(APP_OBJ_RAW ${CMAKE_BINARY_DIR}/apps/${APP_NAME}.raw.o)
    find_program(LLVM_OBJCOPY llvm-objcopy HINTS /opt/homebrew/opt/llvm/bin)
    add_custom_command(OUTPUT ${APP_OBJ}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/apps
        COMMAND ${CXX} ${MODFLAGS} ${APP_MOD_FLAGS} ${APP_EXTRA_FLAGS}
                -c ${APP_SRC} -o ${APP_OBJ_RAW}
        COMMAND ${LLVM_OBJCOPY}
                --localize-symbol=_main --localize-symbol=_Z4mainv
                ${APP_OBJ_RAW} ${APP_OBJ}
        DEPENDS ${HAL_PCM} ${PROC_PCM} ${APP_SRC}
        COMMENT "Compiling ${APP_NAME} (localize main)")

    add_custom_command(OUTPUT ${APP_BIN}
        COMMAND ${CXX} ${MODFLAGS} ${APP_MOD_FLAGS} ${APP_EXTRA_FLAGS}
                ${APP_SRC} ${ALL_OBJS}
                -o ${APP_BIN}
        DEPENDS ${HAL_PCM} ${PROC_PCM} ${ALL_OBJS} ${APP_SRC}
        COMMENT "Building ${APP_NAME}")
    add_custom_target(${APP_NAME} ALL DEPENDS ${APP_BIN})

    list(APPEND ALL_APP_OBJS ${APP_OBJ})
endforeach()

# Shell links with all app .o files (it dispatches to them)
if(EXISTS ${CMAKE_SOURCE_DIR}/src/apps/shell.cpp)
    set(SHELL_BIN ${CMAKE_BINARY_DIR}/shell)
    add_custom_command(OUTPUT ${SHELL_BIN}
        COMMAND ${CXX} ${MODFLAGS} ${APP_MOD_FLAGS}
                ${CMAKE_SOURCE_DIR}/src/apps/shell.cpp
                ${ALL_APP_OBJS} ${ALL_OBJS}
                -o ${SHELL_BIN}
        DEPENDS ${HAL_PCM} ${PROC_PCM} ${ALL_OBJS}
                ${ALL_APP_OBJS}
                ${CMAKE_SOURCE_DIR}/src/apps/shell.cpp
        COMMENT "Building shell (links all apps)")
    add_custom_target(shell ALL DEPENDS ${SHELL_BIN})
endif()

# ─── Poco (Foundation + Net + JSON + Util, host build only) ─────────────────
# BSL-1.0 license. Used for HTTP config server.
# Static libs built in vendor/poco/build-host/.

if(RP4_HOST_BUILD)
    set(POCO_DIR ${CMAKE_SOURCE_DIR}/vendor/poco)
    set(POCO_LIB ${POCO_DIR}/build-host/lib)
    set(POCO_INC
        ${POCO_DIR}/Foundation/include
        ${POCO_DIR}/JSON/include
        ${POCO_DIR}/Net/include
        ${POCO_DIR}/Util/include)
    set(POCO_LIBS
        ${POCO_LIB}/libPocoNet.a
        ${POCO_LIB}/libPocoUtil.a
        ${POCO_LIB}/libPocoJSON.a
        ${POCO_LIB}/libPocoFoundation.a)

    # sqlite3 amalgamation (compiled once, linked into configsrv)
    set(SQLITE3_DIR ${CMAKE_SOURCE_DIR}/vendor/sqlite/sqlite-amalgamation-3480000)
    set(SQLITE3_OBJ ${CMAKE_BINARY_DIR}/sqlite3.o)
    add_custom_command(OUTPUT ${SQLITE3_OBJ}
        COMMAND ${CMAKE_C_COMPILER} -O2 -DSQLITE_THREADSAFE=0
                -c ${SQLITE3_DIR}/sqlite3.c -o ${SQLITE3_OBJ}
        DEPENDS ${SQLITE3_DIR}/sqlite3.c
        COMMENT "Compiling sqlite3 amalgamation")

    # ─── rp4.http jail module (wraps Poco behind C++26 module interface) ────
    set(HTTP_SRC ${CMAKE_SOURCE_DIR}/src/http/http.cppm)
    set(HTTP_PCM ${CMAKE_BINARY_DIR}/rp4.http.pcm)
    set(HTTP_OBJ ${CMAKE_BINARY_DIR}/rp4_http.o)

    set(POCO_ISYSTEM
        -isystem ${POCO_DIR}/Foundation/include
        -isystem ${POCO_DIR}/JSON/include
        -isystem ${POCO_DIR}/Net/include
        -isystem ${POCO_DIR}/Util/include)

    add_custom_command(OUTPUT ${HTTP_PCM}
        COMMAND ${CXX} -std=c++26 -stdlib=libc++ -Wno-reserved-module-identifier
                ${POCO_ISYSTEM}
                --precompile ${HTTP_SRC} -o ${HTTP_PCM}
        DEPENDS ${HTTP_SRC}
        COMMENT "Precompiling rp4.http (Poco jail)")

    add_custom_command(OUTPUT ${HTTP_OBJ}
        COMMAND ${CXX} -std=c++26 -stdlib=libc++ -Wno-reserved-module-identifier
                ${POCO_ISYSTEM}
                -fmodule-file=rp4.http=${HTTP_PCM}
                -c ${HTTP_PCM} -o ${HTTP_OBJ}
        DEPENDS ${HTTP_PCM}
        COMMENT "Compiling rp4.http")

    # ─── configsrv — HTTP config server (imports rp4.http, links Poco+sqlite3) ─
    set(CONFIGSRV_BIN ${CMAKE_BINARY_DIR}/configsrv)
    if(EXISTS ${CMAKE_SOURCE_DIR}/src/apps/configsrv.cpp)
        add_custom_command(OUTPUT ${CONFIGSRV_BIN}
            COMMAND ${CXX} -std=c++26 -stdlib=libc++ -O2
                    -fmodule-file=rp4.http=${HTTP_PCM}
                    -isystem ${SQLITE3_DIR}
                    ${CMAKE_SOURCE_DIR}/src/apps/configsrv.cpp
                    ${HTTP_OBJ}
                    ${SQLITE3_OBJ}
                    ${POCO_LIBS}
                    -o ${CONFIGSRV_BIN}
            DEPENDS ${CMAKE_SOURCE_DIR}/src/apps/configsrv.cpp
                    ${HTTP_PCM} ${HTTP_OBJ}
                    ${SQLITE3_OBJ}
                    ${POCO_LIBS}
            COMMENT "Building configsrv (imports rp4.http)")
        add_custom_target(configsrv ALL DEPENDS ${CONFIGSRV_BIN})
    endif()
endif()

# ─── Tests (Catch2, host build only) ────────────────────────────────────────

if(RP4_HOST_BUILD)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.8.0
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)

    set(TEST_BIN ${CMAKE_BINARY_DIR}/run_tests)
    add_custom_command(OUTPUT ${TEST_BIN}
        COMMAND ${CXX} ${MODFLAGS} ${APP_MOD_FLAGS}
                -isystem ${catch2_SOURCE_DIR}/src
                -isystem ${catch2_BINARY_DIR}/generated-includes
                ${CMAKE_SOURCE_DIR}/test/test_audiomgr.cpp
                ${ALL_OBJS}
                -L ${catch2_BINARY_DIR}/src
                -lCatch2Main -lCatch2
                -o ${TEST_BIN}
        DEPENDS ${HAL_PCM} ${PROC_PCM} ${ALL_OBJS}
                ${CMAKE_SOURCE_DIR}/test/test_audiomgr.cpp
                Catch2 Catch2WithMain
        COMMENT "Building tests")

    add_custom_target(test
        COMMAND ${TEST_BIN} --colour-mode ansi
        DEPENDS ${TEST_BIN}
        COMMENT "Running tests")
endif()

# ─── Z3 verification ────────────────────────────────────────────────────────

find_program(DOTNET dotnet)
if(DOTNET)
    add_custom_target(verify
        COMMAND ${DOTNET} fsi ${CMAKE_SOURCE_DIR}/proof/run_all.fsx
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running Z3 verification + generating proof certificates")
endif()

# ─── CBMC verification ──────────────────────────────────────────────────────

find_program(CBMC cbmc)
if(CBMC)
    add_custom_target(verify-shim
        COMMAND ${CBMC} ${CMAKE_SOURCE_DIR}/spec/verify_shim.c
                --unwind 17 --bounds-check --pointer-check
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running CBMC on libc shim (153 checks)")
endif()

# ─── Doxygen ratchet ─────────────────────────────────────────────────────────

find_program(DOXYGEN doxygen)
if(DOXYGEN)
    set(DOXY_LOG ${CMAKE_BINARY_DIR}/doxygen-warnings.log)
    set(DOXY_BASELINE ${CMAKE_SOURCE_DIR}/tools/doxygen/baseline.txt)
    set(RATCHET ${CMAKE_SOURCE_DIR}/tools/doxygen/ratchet.sh)

    add_custom_target(doc-coverage-raw
        COMMAND ${DOXYGEN} ${CMAKE_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running doxygen")

    add_custom_target(doc-coverage
        COMMAND ${RATCHET} --strip-prefix "${CMAKE_SOURCE_DIR}/" ${DOXY_LOG} ${DOXY_BASELINE}
        DEPENDS doc-coverage-raw
        COMMENT "Checking doc-coverage ratchet")

    add_custom_target(doc-coverage-update
        COMMAND ${RATCHET} --update --strip-prefix "${CMAKE_SOURCE_DIR}/" ${DOXY_LOG} ${DOXY_BASELINE}
        DEPENDS doc-coverage-raw
        COMMENT "Updating doc-coverage baseline")
endif()

# ─── Status ──────────────────────────────────────────────────────────────────

message(STATUS "")
message(STATUS "=== rp4 ===")
message(STATUS "kernel:   ${KERNEL_DIR} (read-only)")
message(STATUS "clang:    ${CMAKE_CXX_COMPILER}")
message(STATUS "mode:     host build")
message(STATUS "")
message(STATUS "=== MODULES ===")
message(STATUS "  rp4.hal          BCM2711 hardware abstraction")
message(STATUS "  rp4.proc         Process manager")
if(RP4_HOST_BUILD)
    message(STATUS "  rp4.http         Poco HTTP jail (host-only)")
endif()
message(STATUS "")
message(STATUS "=== APPS (auto-discovered) ===")
foreach(APP_SRC ${APP_SOURCES})
    get_filename_component(APP_NAME ${APP_SRC} NAME_WE)
    if(NOT APP_NAME STREQUAL "configsrv")
        message(STATUS "  ${APP_NAME}")
    endif()
endforeach()
if(RP4_HOST_BUILD AND EXISTS "${CMAKE_SOURCE_DIR}/src/apps/configsrv.cpp")
    message(STATUS "  configsrv        (host-only, Poco::Net)")
endif()
message(STATUS "")
message(STATUS "=== TARGETS ===")
message(STATUS "  cmake --build build                     Build all apps")
if(RP4_HOST_BUILD)
    message(STATUS "  cmake --build build --target test       Run Catch2 tests")
endif()
message(STATUS "  cmake --build build --target verify      Run Z3 proofs (20 assertions)")
if(CBMC)
    message(STATUS "  cmake --build build --target verify-shim Run CBMC (153 checks)")
endif()
if(DOXYGEN)
    message(STATUS "  cmake --build build --target doc-coverage Check ratchet")
endif()
message(STATUS "")
message(STATUS "=== CROSS BUILD ===")
message(STATUS "  ./scripts/cross-build.sh                Bare-metal kernel8.img")
message(STATUS "")
