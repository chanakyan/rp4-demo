// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// build-kodi.fsx — Build minimal Linux + Kodi image via Buildroot in Docker
//
// Output:
//   firmware/kodi_kernel.img      — Linux kernel (Image)
//   firmware/kodi_rootfs.squashfs — Root filesystem with Kodi
//
// Requires: OrbStack (docker)
//
// Usage: dotnet fsi scripts/build-kodi.fsx

#load "../lib/shell/Shell.fsx"
open Shell
open System.IO

let repo = expand "~/rp4"
let docker = expand "~/.orbstack/bin/docker"

printfn "=== rp4 Kodi image builder ==="
printfn "repo: %s" repo
printfn "docker: %s" docker

// Write temp Dockerfile
let dfPath = $"{repo}/build-kodi/Dockerfile.tmp"
Directory.CreateDirectory(dirname dfPath) |> ignore
write dfPath """FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential gcc g++ make patch gawk \
    wget cpio unzip rsync bc \
    python3 python3-setuptools \
    file which sed \
    libncurses-dev \
    git ca-certificates \
    squashfs-tools \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
"""

// Build Docker image
sh $"{docker} build -t buildroot-kodi -f {dfPath} {repo}" |> ignore
File.Delete(dfPath)
printfn "=== Docker image ready ==="

// Run the build
let buildScript =
    "set -euo pipefail && " +
    "cp -a /buildroot /build/buildroot && " +
    "cd /build/buildroot && " +
    "cp /buildroot-custom/kodi_defconfig configs/rp4_kodi_defconfig && " +
    "make rp4_kodi_defconfig O=/output && " +
    "make -j$(nproc) O=/output && " +
    "echo '=== Build complete ===' && " +
    "ls -lh /output/images/"

sh ($"{docker} run --rm --name buildroot-kodi-build " +
    $"-v {repo}/vendor/buildroot:/buildroot:ro " +
    $"-v {repo}/scripts/kodi_defconfig:/buildroot-custom/kodi_defconfig:ro " +
    $"-v {repo}/build-kodi:/output " +
    $"buildroot-kodi bash -c \"{buildScript}\"") |> ignore

// Copy outputs
let firmware = $"{repo}/firmware"
Directory.CreateDirectory(firmware) |> ignore

let kernelImg = $"{repo}/build-kodi/images/Image"
if exists kernelImg then
    File.Copy(kernelImg, $"{firmware}/kodi_kernel.img", true)
    printfn "=== copied kodi_kernel.img ==="

let rootfs = $"{repo}/build-kodi/images/rootfs.squashfs"
if exists rootfs then
    File.Copy(rootfs, $"{firmware}/kodi_rootfs.squashfs", true)
    printfn "=== copied kodi_rootfs.squashfs ==="

printfn "=== done ==="
