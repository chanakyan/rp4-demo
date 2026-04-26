#!/usr/bin/env dotnet fsi
// SPDX-License-Identifier: BSD-2-Clause
// dumpifs.fsx — Dump a QNX IFS image for verification.

open System
open System.IO
open System.Text

let args = fsi.CommandLineArgs |> Array.toList |> List.tail
let ifsPath = match args with "--" :: p :: _ -> p | p :: _ -> p | _ -> failwith "usage: dumpifs.fsx <file.ifs>"

let data = File.ReadAllBytes(ifsPath)
let r = new BinaryReader(new MemoryStream(data))

// Header
let signature = Encoding.ASCII.GetString(r.ReadBytes(7))
let flags = r.ReadByte()
let imageSize = r.ReadUInt32()
let hdrDirSize = r.ReadUInt32()
let dirOffset = r.ReadUInt32()
let bootIno = [| for _ in 0..3 -> r.ReadUInt32() |]
let scriptIno = r.ReadUInt32()
let chainPaddr = r.ReadUInt32()
let spare = [| for _ in 0..9 -> r.ReadUInt32() |]
let mountFlags = r.ReadUInt32()

// Read mountpoint (null-terminated)
let mutable mpBytes = []
let mutable b = r.ReadByte()
while b <> 0uy do
    mpBytes <- b :: mpBytes
    b <- r.ReadByte()
let mountpoint = Encoding.ASCII.GetString(List.rev mpBytes |> List.toArray)

printfn "─── IFS Header ───"
printfn "  signature:   %s" signature
printfn "  flags:       0x%02X" flags
printfn "  image_size:  %d (0x%X)" imageSize imageSize
printfn "  hdr_dir_sz:  %d" hdrDirSize
printfn "  dir_offset:  %d" dirOffset
printfn "  script_ino:  %d" scriptIno
printfn "  mountpoint:  %s" mountpoint
printfn ""

// Seek to directory
r.BaseStream.Position <- int64 dirOffset
// Align to 4
let alignPos = int64 dirOffset + int64 ((4 - (int dirOffset % 4)) % 4)
r.BaseStream.Position <- alignPos

printfn "─── Directory ───"
printfn "  %-6s %-10s %-6s %-6s %-8s %-8s %s" "ino" "mode" "uid" "gid" "offset" "size" "path"

let mutable reading = true
while reading && r.BaseStream.Position < int64 hdrDirSize do
    let pos = r.BaseStream.Position
    let entrySize = r.ReadUInt16()
    if entrySize = 0us then
        reading <- false
    else
        let _extattr = r.ReadUInt16()
        let ino = r.ReadUInt32()
        let mode = r.ReadUInt32()
        let gid = r.ReadUInt32()
        let uid = r.ReadUInt32()
        let mtime = r.ReadUInt32()
        let isDir = (mode &&& 0o170000u) = 0o040000u
        if isDir then
            let remaining = int entrySize - 20
            let pathBytes = r.ReadBytes(remaining)
            let path = Encoding.ASCII.GetString(pathBytes).TrimEnd('\x00')
            printfn "  %-6d 0o%06o   %-6d %-6d %-8s %-8s %s" ino mode uid gid "---" "---" path
        else
            let offset = r.ReadUInt32()
            let size = r.ReadUInt32()
            let remaining = int entrySize - 28
            let pathBytes = r.ReadBytes(remaining)
            let path = Encoding.ASCII.GetString(pathBytes).TrimEnd('\x00')
            printfn "  %-6d 0o%06o   %-6d %-6d %-8d %-8d %s" ino mode uid gid offset size path
        r.BaseStream.Position <- pos + int64 entrySize

// Checksum
let trailerPos = int imageSize - 4
let storedCksum = BitConverter.ToUInt32(data, trailerPos)
// Compute checksum over data[0..trailerPos-1]
let mutable sum = 0u
for i in 0 .. 4 .. trailerPos - 1 do
    if i + 4 <= trailerPos then
        sum <- sum + BitConverter.ToUInt32(data, i)
let expected = ~~~sum + 1u
printfn ""
printfn "─── Trailer ───"
printfn "  stored:   0x%08X" storedCksum
printfn "  computed: 0x%08X" expected
printfn "  valid:    %b" (storedCksum = expected)
