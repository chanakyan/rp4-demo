// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// boot.fsx — flash SD card and prepare RPi4 boot
//
// Usage:
//   dotnet fsi scripts/boot.fsx                    # detect SD card, flash
//   dotnet fsi scripts/boot.fsx -- /dev/diskN      # specify disk
//   dotnet fsi scripts/boot.fsx -- --dry-run       # show what would happen

#load "../lib/shell/Shell.fsx"
open Shell
open System
open System.IO

let repo = expand "~/rp4"
let kernel = $"{repo}/build-rpi/kernel8.img"
let fw = $"{repo}/firmware"

// Preflight
if not (exists kernel) then failwith "kernel8.img not found. Run: cmake --build build-rpi"
if not (exists fw) then failwith "firmware/ not found."
for f in ["start4.elf"; "config.txt"] do
    if not (exists $"{fw}/{f}") then failwithf "%s missing from firmware/" f

// Parse args
let args = fsi.CommandLineArgs |> Array.skip 1 |> Array.toList
let dryRun, diskArg =
    match args with
    | ["--dry-run"] -> true, "/dev/diskX"
    | [d] -> false, d
    | [] -> false, ""
    | _ -> failwith "Usage: dotnet fsi boot.fsx -- [--dry-run | /dev/diskN]"

let disk =
    if diskArg <> "" then diskArg
    else
        printfn "=== Detecting SD card ==="
        sh "diskutil list external" |> printfn "%s"
        printf "Enter disk (e.g. disk6): "
        $"/dev/{Console.ReadLine()}"

printfn ""
printfn "=== FLASH PLAN ==="
printfn "  disk:       %s" disk
printfn "  kernel:     %s (%d bytes)" kernel (fileSize kernel)
printfn "  firmware:   %s/" fw
printfn ""
printfn "  Files to copy:"
printfn "    start4.elf           GPU firmware (Broadcom)"
printfn "    fixup4.dat           GPU memory fixup"
printfn "    bcm2711-rpi-4-b.dtb  Device tree"
printfn "    config.txt           Boot config (arm_64bit=1, enable_uart=1)"
printfn "    kernel8.img          OUR kernel (BSD-2)"

let circleExists = exists $"{repo}/build-rpi/circle_drv.elf"
if circleExists then
    printfn "    circle_drv.elf       Driver process (GPL-3, IPC-isolated)"

if dryRun then
    printfn "\n  (dry run — no changes made)"
    exit 0

printfn "\n  WARNING: This will ERASE %s and format as FAT32." disk
printf  "  Type YES to proceed: "
if Console.ReadLine() <> "YES" then
    printfn "Aborted."
    exit 1

// Format
printfn "\n=== Formatting %s as FAT32 ===" disk
sh $"diskutil eraseDisk FAT32 BOOT MBR {disk}" |> ignore

// Find mount point
let mountPoint =
    sh "mount"
    |> fun s -> s.Split('\n')
    |> Array.tryFind (fun l -> l.Contains($"{disk}s1"))
    |> Option.map (fun l -> (l.Split(" on ").[1]).Split(" (").[0])
    |> Option.defaultValue "/Volumes/BOOT"

if not (Directory.Exists(mountPoint)) then
    failwithf "Could not find mount point for %s" disk

printfn "  mounted: %s" mountPoint

// Copy files
printfn "\n=== Copying files ==="
let copy src dst =
    if exists src then
        File.Copy(src, $"{mountPoint}/{dst}", true)
        printfn "  ok %s" dst

copy $"{fw}/start4.elf" "start4.elf"
copy $"{fw}/fixup4.dat" "fixup4.dat"
copy $"{fw}/bcm2711-rpi-4-b.dtb" "bcm2711-rpi-4-b.dtb"
copy $"{fw}/config.txt" "config.txt"
copy kernel "kernel8.img"
if circleExists then copy $"{repo}/build-rpi/circle_drv.elf" "circle_drv.elf"

// Verify
printfn "\n=== SD card contents ==="
for f in Directory.GetFiles(mountPoint) do
    let info = FileInfo(f)
    printfn "  %8d  %s" info.Length info.Name

// Eject
printfn "\n=== Ejecting ==="
sh $"diskutil eject {disk}" |> ignore
printfn "  Safe to remove"

printfn "\nInsert SD card into RPi4. Power on. LED blinks = kernel is alive."
