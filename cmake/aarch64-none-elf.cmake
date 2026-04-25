# SPDX-License-Identifier: BSD-2-Clause
# Toolchain file: aarch64 bare-metal using Homebrew LLVM
# Usage: cmake -S . -B build-rpi -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-none-elf.cmake

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Use Homebrew LLVM (supports all targets out of the box)
execute_process(
    COMMAND brew --prefix llvm
    OUTPUT_VARIABLE LLVM_PREFIX
    OUTPUT_STRIP_TRAILING_WHITESPACE)

set(CMAKE_C_COMPILER   "${LLVM_PREFIX}/bin/clang")
set(CMAKE_CXX_COMPILER "${LLVM_PREFIX}/bin/clang++")
set(CMAKE_ASM_COMPILER "${LLVM_PREFIX}/bin/clang")
set(CMAKE_OBJCOPY      "${LLVM_PREFIX}/bin/llvm-objcopy")
set(CMAKE_AR           "${LLVM_PREFIX}/bin/llvm-ar")
set(CMAKE_RANLIB       "${LLVM_PREFIX}/bin/llvm-ranlib")

set(TARGET_TRIPLE aarch64-none-elf)

set(CMAKE_C_FLAGS   "--target=${TARGET_TRIPLE} -mcpu=cortex-a72 -ffreestanding -nostdlib")
set(CMAKE_CXX_FLAGS "--target=${TARGET_TRIPLE} -mcpu=cortex-a72 -ffreestanding -nostdlib -fno-exceptions -fno-rtti")
set(CMAKE_ASM_FLAGS "--target=${TARGET_TRIPLE} -mcpu=cortex-a72")

set(CMAKE_EXE_LINKER_FLAGS "-nostdlib -T ${CMAKE_SOURCE_DIR}/src/boot/link.ld")

set(RP4_HOST_BUILD OFF CACHE BOOL "" FORCE)

# No standard library available in freestanding mode
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)
