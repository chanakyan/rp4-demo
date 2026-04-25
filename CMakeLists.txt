# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.20)
project(rp4-demo C CXX ASM)

# ─── rp4-demo ───────────────────────────────────────────────────────────────
#
# Public-facing demo of rp4 resource managers.
# Code only — no vendored libs, no submodules.
# Full build requires the private rp4 repo.
#
# ─────────────────────────────────────────────────────────────────────────────

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "")
message(STATUS "=== rp4-demo ===")
message(STATUS "This is the public demo repo. Code is browsable.")
message(STATUS "Full build requires lib/qnx-micro, lib/bionic, vendor/poco.")
message(STATUS "See proof/certs/ for Z3 verification certificates.")
message(STATUS "")
