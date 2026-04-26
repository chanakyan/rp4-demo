#!/usr/bin/env dotnet fsi
// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// mkifs.fsx — Build a QNX IFS (Image Filesystem) for aarch64 bare-metal.
//
// Reads a .build file, packs files into an IFS image with:
//   image_header  ("imagefs" signature, directory offsets)
//   image_dirent  (file/dir entries with mode, uid, gid, mtime)
//   file data     (raw bytes, 4-byte aligned)
//   image_trailer (CRC32 checksum)
//
// Reference: vendor/openqnx/trunk/services/system/public/sys/image.h
//
// Usage: dotnet fsi scripts/mkifs.fsx -- rp4.build output.ifs
//
// No compression. No ELF relocation. No endian swapping (native LE).
// This is the minimum viable mkifs for bare-metal aarch64.

open System
open System.IO
open System.Text

// ─── IFS on-disk structures (from sys/image.h) ─────────────────────────────

[<Literal>]
let IMAGE_SIGNATURE = "imagefs"

[<Literal>]
let IMAGE_FLAGS_READONLY = 0x02uy

// image_header: 56 bytes + mountpoint (padded to 4-byte align)
// signature[7] flags[1] image_size[4] hdr_dir_size[4] dir_offset[4]
// boot_ino[16] script_ino[4] chain_paddr[4] spare[40] mountflags[4]
// mountpoint[variable, null-terminated, padded to 4]

let writeHeader (w: BinaryWriter) imageSize hdrDirSize dirOffset scriptIno mountpoint =
    // signature (7 bytes)
    w.Write(Encoding.ASCII.GetBytes(IMAGE_SIGNATURE))
    // flags (1 byte) — little-endian, readonly
    w.Write(IMAGE_FLAGS_READONLY)
    // image_size (4 bytes)
    w.Write(uint32 imageSize)
    // hdr_dir_size (4 bytes)
    w.Write(uint32 hdrDirSize)
    // dir_offset (4 bytes)
    w.Write(uint32 dirOffset)
    // boot_ino[4] (16 bytes) — no bootstrap executables
    for _ in 0..3 do w.Write(0u)
    // script_ino (4 bytes)
    w.Write(uint32 scriptIno)
    // chain_paddr (4 bytes)
    w.Write(0u)
    // spare[10] (40 bytes)
    for _ in 0..9 do w.Write(0u)
    // mountflags (4 bytes)
    w.Write(0u)
    // mountpoint (null-terminated, padded to 4-byte align)
    let mp = Encoding.ASCII.GetBytes(mountpoint + "\x00")
    w.Write(mp)
    let pad = (4 - (mp.Length % 4)) % 4
    if pad > 0 then w.Write(Array.zeroCreate<byte> pad)

// image_dirent for a regular file:
// size[2] extattr_offset[2] ino[4] mode[4] gid[4] uid[4] mtime[4]
// offset[4] filesize[4] path[variable, null-terminated]
// Total: 28 + path length, padded to 4

let S_IFREG = 0o100000u
let S_IFDIR = 0o040000u

let writeFileDirent (w: BinaryWriter) ino mode gid uid mtime offset size (path: string) =
    let pathBytes = Encoding.ASCII.GetBytes(path + "\x00")
    let entrySize = 28 + pathBytes.Length
    let paddedSize = entrySize + ((4 - (entrySize % 4)) % 4)
    // attr
    w.Write(uint16 paddedSize)     // size
    w.Write(0us)                   // extattr_offset
    w.Write(uint32 ino)            // ino
    w.Write(uint32 mode)           // mode
    w.Write(uint32 gid)            // gid
    w.Write(uint32 uid)            // uid
    w.Write(uint32 mtime)          // mtime
    // file-specific
    w.Write(uint32 offset)         // offset from header
    w.Write(uint32 size)           // file size
    w.Write(pathBytes)
    let pad = paddedSize - entrySize
    if pad > 0 then w.Write(Array.zeroCreate<byte> pad)
    paddedSize

let writeDirDirent (w: BinaryWriter) ino mode gid uid mtime (path: string) =
    let pathBytes = Encoding.ASCII.GetBytes(path + "\x00")
    let entrySize = 20 + pathBytes.Length
    let paddedSize = entrySize + ((4 - (entrySize % 4)) % 4)
    w.Write(uint16 paddedSize)
    w.Write(0us)
    w.Write(uint32 ino)
    w.Write(uint32 mode)
    w.Write(uint32 gid)
    w.Write(uint32 uid)
    w.Write(uint32 mtime)
    w.Write(pathBytes)
    let pad = paddedSize - entrySize
    if pad > 0 then w.Write(Array.zeroCreate<byte> pad)
    paddedSize

// Sentinel: 2-byte zero size terminates directory
let writeDirEnd (w: BinaryWriter) =
    w.Write(0us)
    2

// image_trailer: CRC32 checksum (4 bytes)
// Checksum is negated sum of all uint32 words from header to trailer start
let computeChecksum (data: byte[]) =
    let mutable sum = 0u
    let len = data.Length / 4 * 4
    for i in 0 .. 4 .. len - 1 do
        let word = BitConverter.ToUInt32(data, i)
        sum <- sum + word
    // Handle trailing bytes (pad with zeros conceptually)
    if data.Length % 4 <> 0 then
        let trailing = Array.zeroCreate<byte> 4
        Array.Copy(data, len, trailing, 0, data.Length - len)
        sum <- sum + BitConverter.ToUInt32(trailing, 0)
    ~~~sum + 1u  // -sum in uint32 = ~~~sum + 1

// ─── .build file parser ────────────────────────────────────────────────────

type BuildEntry =
    | FileEntry of target: string * host: string * perms: int
    | DirEntry of path: string * perms: int
    | ScriptEntry of lines: string list

let parseBuildFile (path: string) =
    let lines = File.ReadAllLines(path) |> Array.toList
    let mutable entries = []
    let mutable scriptLines = []
    let mutable inScript = false
    let mutable inDataSection = false

    for line in lines do
        let trimmed = line.Trim()
        if trimmed = "" || trimmed.StartsWith("#") then
            ()  // skip comments and blanks
        elif trimmed = "[+script] startup-script = {" then
            inScript <- true
            scriptLines <- []
        elif inScript && trimmed = "}" then
            inScript <- false
            entries <- ScriptEntry(List.rev scriptLines) :: entries
        elif inScript then
            scriptLines <- trimmed :: scriptLines
        elif trimmed.StartsWith("[data=") then
            inDataSection <- true
        elif trimmed.StartsWith("[") then
            ()  // skip other section headers like [virtual=...]
        elif inDataSection then
            // Lines in [data=c] are just filenames: target=host or just name
            if trimmed.Contains("=") then
                let parts = trimmed.Split('=', 2)
                entries <- FileEntry(parts.[0].Trim(), parts.[1].Trim(), 0o755) :: entries
            else
                entries <- FileEntry(trimmed, trimmed, 0o755) :: entries
        else
            // Pre-data section: library lines like "libc.so" or "name=host"
            if trimmed.Contains("=") then
                let parts = trimmed.Split('=', 2)
                entries <- FileEntry(parts.[0].Trim(), parts.[1].Trim(), 0o755) :: entries
            elif not (trimmed.StartsWith("[")) then
                entries <- FileEntry(trimmed, trimmed, 0o644) :: entries

    List.rev entries

// ─── Build the IFS image ───────────────────────────────────────────────────

let buildIfs (buildFile: string) (outputPath: string) (searchPaths: string list) =
    let entries = parseBuildFile buildFile
    let buildDir = Path.GetDirectoryName(Path.GetFullPath(buildFile))

    // Resolve host file path
    let resolveHost (name: string) =
        let candidates =
            [ name
              Path.Combine(buildDir, name) ]
            @ [ for sp in searchPaths do
                    yield Path.Combine(sp, name)
                    yield Path.Combine(sp, Path.GetFileName(name)) ]
        candidates |> List.tryFind File.Exists

    // Collect files with their data
    let mutable fileEntries = []
    let mutable scriptData = None
    let mutable inoCounter = 1

    for entry in entries do
        match entry with
        | ScriptEntry lines ->
            let text = String.Join("\n", lines) + "\n"
            let data = Encoding.ASCII.GetBytes(text)
            scriptData <- Some(inoCounter, data)
            inoCounter <- inoCounter + 1
        | FileEntry(target, host, perms) ->
            match resolveHost host with
            | Some hostPath ->
                let data = File.ReadAllBytes(hostPath)
                let info = FileInfo(hostPath)
                let mtime = int (DateTimeOffset(info.LastWriteTimeUtc).ToUnixTimeSeconds())
                fileEntries <- (target, data, perms, mtime, inoCounter) :: fileEntries
                inoCounter <- inoCounter + 1
            | None ->
                eprintfn "warning: file not found: %s (skipping)" host
        | DirEntry(path, perms) ->
            inoCounter <- inoCounter + 1

    let fileEntries = List.rev fileEntries
    let mountpoint = "/proc/boot"

    // Phase 1: compute sizes
    // Header size
    let headerMs = new MemoryStream()
    let headerW = new BinaryWriter(headerMs)
    writeHeader headerW 0 0 0 0 mountpoint
    let headerSize = int headerMs.Length
    headerMs.Dispose()

    // Directory entries size
    let dirMs = new MemoryStream()
    let dirW = new BinaryWriter(dirMs)

    // Script dirent (if any)
    match scriptData with
    | Some(ino, _) ->
        writeFileDirent dirW ino (S_IFREG ||| 0o755u) 0u 0u 0 0 0 ".script" |> ignore
    | None -> ()

    // File dirents (offset placeholder = 0, we'll fix in phase 2)
    for (target, data, perms, mtime, ino) in fileEntries do
        writeFileDirent dirW ino (S_IFREG ||| uint32 perms) 0u 0u mtime 0 data.Length target |> ignore

    writeDirEnd dirW |> ignore
    let dirSize = int dirMs.Length
    dirMs.Dispose()

    let dirOffset = headerSize
    let hdrDirSize = headerSize + dirSize

    // Compute file data offsets
    let mutable fileOffset = hdrDirSize
    let mutable fileOffsets = []

    match scriptData with
    | Some(_, data) ->
        fileOffsets <- (fileOffset, data) :: fileOffsets
        fileOffset <- fileOffset + data.Length
        fileOffset <- fileOffset + ((4 - (fileOffset % 4)) % 4)
    | None -> ()

    for (_, data, _, _, _) in fileEntries do
        fileOffsets <- (fileOffset, data) :: fileOffsets
        fileOffset <- fileOffset + data.Length
        fileOffset <- fileOffset + ((4 - (fileOffset % 4)) % 4)

    let fileOffsets = List.rev fileOffsets
    let trailerOffset = fileOffset
    let imageSize = trailerOffset + 4  // 4 bytes for trailer

    // Phase 2: write the actual image
    let ms = new MemoryStream()
    let w = new BinaryWriter(ms)

    let scriptIno = match scriptData with Some(ino, _) -> ino | None -> 0

    // Header
    writeHeader w imageSize hdrDirSize dirOffset scriptIno mountpoint

    // Directory (with correct offsets now)
    let mutable offsetIdx = 0

    match scriptData with
    | Some(ino, data) ->
        let (off, _) = fileOffsets.[offsetIdx]
        offsetIdx <- offsetIdx + 1
        writeFileDirent w ino (S_IFREG ||| 0o755u) 0u 0u 0 off data.Length ".script" |> ignore
    | None -> ()

    for (target, data, perms, mtime, ino) in fileEntries do
        let (off, _) = fileOffsets.[offsetIdx]
        offsetIdx <- offsetIdx + 1
        writeFileDirent w ino (S_IFREG ||| uint32 perms) 0u 0u mtime off data.Length target |> ignore

    writeDirEnd w |> ignore

    // File data
    for (off, data) in fileOffsets do
        // Pad to reach offset
        let current = int ms.Position
        if current < off then
            w.Write(Array.zeroCreate<byte> (off - current))
        w.Write(data)

    // Pad to trailer
    let current = int ms.Position
    if current < trailerOffset then
        w.Write(Array.zeroCreate<byte> (trailerOffset - current))

    // Compute checksum over everything written so far
    let imageData = ms.ToArray()
    let cksum = computeChecksum imageData
    w.Write(cksum)

    // Write to output file
    let finalData = ms.ToArray()
    File.WriteAllBytes(outputPath, finalData)
    ms.Dispose()

    printfn "mkifs: %s -> %s" buildFile outputPath
    printfn "  image size:  %d bytes (%d KB)" finalData.Length (finalData.Length / 1024)
    printfn "  header:      %d bytes" headerSize
    printfn "  directory:   %d bytes (%d entries)" dirSize (fileEntries.Length + (if scriptData.IsSome then 1 else 0))
    printfn "  files:       %d" fileEntries.Length
    if scriptData.IsSome then
        printfn "  script:      yes (ino %d)" scriptIno
    printfn "  checksum:    0x%08X" cksum

// ─── Main ──────────────────────────────────────────────────────────────────

let rawArgs = fsi.CommandLineArgs |> Array.toList |> List.tail  // skip script name

// Skip "--" if present
let args =
    match rawArgs with
    | "--" :: rest -> rest
    | _ -> rawArgs

match args with
| buildFile :: outputPath :: searchPaths ->
    if not (File.Exists buildFile) then
        eprintfn "error: build file not found: %s" buildFile
        exit 1
    buildIfs buildFile outputPath searchPaths
| [ buildFile ] ->
    let output = Path.ChangeExtension(buildFile, ".ifs")
    buildIfs buildFile output []
| _ ->
    eprintfn "usage: dotnet fsi mkifs.fsx -- <buildfile> [output.ifs] [search paths...]"
    exit 1
