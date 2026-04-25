// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// seed_config.fsx — detect tools, seed config.db
//
// Ingest script (raw SQL OK — this CREATES the db, reads use SqlHydra).
//
// Usage: dotnet fsi scripts/seed_config.fsx -- config.db

#r "nuget: Microsoft.Data.Sqlite, 9.0.4"
open System
open System.IO
open System.Diagnostics
open Microsoft.Data.Sqlite

// ── Args (fsi passes: dotnet, fsi, scriptpath, --, user args) ───────────

let userArgs = fsi.CommandLineArgs |> Array.skip 1
if userArgs.Length < 1 then
    eprintfn "Usage: dotnet fsi scripts/seed_config.fsx -- <config.db>"
    exit 1

let dbPath = userArgs.[0]
printfn "  output: %s" dbPath

if File.Exists(dbPath) then File.Delete(dbPath)
let conn = new SqliteConnection(sprintf "Data Source=%s" dbPath)
conn.Open()

let exec sql =
    use cmd = conn.CreateCommand()
    cmd.CommandText <- sql
    cmd.ExecuteNonQuery() |> ignore

// ── Schema ──────────────────────────────────────────────────────────────

exec "CREATE TABLE tool (
    name     TEXT PRIMARY KEY,
    path     TEXT,
    version  TEXT,
    required INTEGER DEFAULT 1
)"

exec "CREATE TABLE project (
    name     TEXT PRIMARY KEY,
    repo     TEXT NOT NULL,
    license  TEXT NOT NULL DEFAULT 'BSD-2-Clause',
    submod   TEXT
)"

exec "CREATE TABLE build (
    key      TEXT PRIMARY KEY,
    value    TEXT NOT NULL
)"

exec "CREATE TABLE substrate (
    name       TEXT PRIMARY KEY,
    resource   TEXT NOT NULL,
    ref_left   TEXT NOT NULL,
    ref_right  TEXT NOT NULL,
    domain     TEXT
)"

// ── Tool detection ──────────────────────────────────────────────────────

let findTool (name: string) (candidates: string list) : string option =
    candidates |> List.tryFind File.Exists
    |> Option.orElseWith (fun () ->
        try
            let psi = ProcessStartInfo("which", name)
            psi.RedirectStandardOutput <- true
            psi.UseShellExecute <- false
            let p = Process.Start(psi)
            let path = p.StandardOutput.ReadToEnd().Trim()
            p.WaitForExit()
            if p.ExitCode = 0 && path <> "" then Some path else None
        with _ -> None)

let getVersion (path: string) (flag: string) : string =
    try
        let psi = ProcessStartInfo(path, flag)
        psi.RedirectStandardOutput <- true
        psi.RedirectStandardError <- true
        psi.UseShellExecute <- false
        let p = Process.Start(psi)
        let out = p.StandardOutput.ReadToEnd() + p.StandardError.ReadToEnd()
        p.WaitForExit()
        out.Trim().Split('\n').[0]
    with _ -> "unknown"

let insertTool name path version required =
    use cmd = conn.CreateCommand()
    cmd.CommandText <- "INSERT INTO tool (name, path, version, required) VALUES (@n, @p, @v, @r)"
    cmd.Parameters.AddWithValue("@n", name) |> ignore
    cmd.Parameters.AddWithValue("@p", path |> Option.defaultValue "") |> ignore
    cmd.Parameters.AddWithValue("@v", version) |> ignore
    cmd.Parameters.AddWithValue("@r", if required then 1L else 0L) |> ignore
    cmd.ExecuteNonQuery() |> ignore

let tools = [
    "clang",        ["/opt/homebrew/opt/llvm/bin/clang"; "/usr/local/opt/llvm/bin/clang"],           "--version", true
    "clang++",      ["/opt/homebrew/opt/llvm/bin/clang++"; "/usr/local/opt/llvm/bin/clang++"],       "--version", true
    "ld.lld",       ["/opt/homebrew/bin/ld.lld"; "/usr/bin/ld.lld"],                                "--version", true
    "llvm-objcopy", ["/opt/homebrew/opt/llvm/bin/llvm-objcopy"],                                    "--version", true
    "llvm-size",    ["/opt/homebrew/opt/llvm/bin/llvm-size"],                                       "--version", false
    "llvm-ar",      ["/opt/homebrew/opt/llvm/bin/llvm-ar"],                                         "--version", true
    "z3",           ["/opt/homebrew/bin/z3"; "/usr/bin/z3"],                                         "--version", true
    "dotnet",       ["/opt/homebrew/bin/dotnet"; "/usr/local/share/dotnet/dotnet"],                  "--version", true
    "cbmc",         ["/opt/homebrew/bin/cbmc"; "/usr/bin/cbmc"],                                     "--version", false
    "doxygen",      ["/opt/homebrew/bin/doxygen"; "/usr/bin/doxygen"],                               "--version", false
    "meson",        ["/opt/homebrew/bin/meson"; "/usr/bin/meson"],                                   "--version", false
    "ninja",        ["/opt/homebrew/bin/ninja"; "/usr/bin/ninja"],                                   "--version", false
]

printfn "=== Detecting tools ==="
for (name, candidates, vflag, required) in tools do
    let path = findTool name candidates
    let version = match path with Some p -> getVersion p vflag | None -> ""
    insertTool name path version required
    let status = match path with Some _ -> "✓" | None -> if required then "✗ MISSING" else "-"
    printfn "  %-16s %s %s" name status (path |> Option.defaultValue "")

// ── Projects ────────────────────────────────────────────────────────────

let insertRow table (cols: string) (vals: string) =
    exec (sprintf "INSERT INTO %s (%s) VALUES (%s)" table cols vals)

insertRow "project" "name, repo, license, submod" "'rp4', 'https://github.com/chanakyan/rp4', 'BSD-2-Clause', NULL"
insertRow "project" "name, repo, license, submod" "'qnx-micro', 'https://github.com/chanakyan/qnx-micro', 'BSD-2-Clause', 'lib/qnx-micro'"
insertRow "project" "name, repo, license, submod" "'alloy-fsx', 'https://github.com/chanakyan/alloy-fsx', 'BSD-2-Clause', 'lib/alloy-fsx'"
insertRow "project" "name, repo, license, submod" "'tca', 'https://github.com/chanakyan/tca', 'BSD-2-Clause', NULL"

// ── Build config ────────────────────────────────────────────────────────

for (k, v) in [
    "target",          "aarch64-none-elf"
    "cpu",             "cortex-a72"
    "c_std",           "c99"
    "cxx_std",         "c++26"
    "sysroot_headers", "vendor/picolibc/libc/include"
    "sysroot_picolibc","build-picolibc"
    "sysroot_libcxx",  "build-libcxx/include/c++/v1"
    "linker_script",   "src/boot/link.ld"
    "load_address",    "0x80000"
    // Allocator
    "arena_size",      "1048576"
    "block_hdr",       "24"
    "min_alloc",       "16"
    "alignment",       "16"
    // Kernel resource bounds
    "max_threads",     "256"
    "thread_size",     "48"
    "max_channels",    "256"
    "channel_size",    "128"
    "max_connections", "512"
    "connection_size", "32"
    "max_caps",        "256"
    "cap_size",        "64"
    "max_buffers",     "16"
    "buffer_overhead", "32"
    "max_audio_frame", "3840"
    "max_pps_objects",  "64"
    "pps_object_size", "256"
    "max_reply_slots", "256"
    "reply_slot_size", "48"
    "worst_case_kernel","658000"
    // Audio
    "sample_rate",     "48000"
    "bits_per_sample", "16"
    "channels",        "2"
    "frame_ms",        "10"
    "frame_bytes",     "1920"
    "half_frame",      "1920"
    // Config server
    "configsrv_port",  "8080"
    "configsrv_dist",  "dist"
] do insertRow "build" "key, value" (sprintf "'%s', '%s'" k v)

// ── Substrates ──────────────────────────────────────────────────────────

for (n, r, l, ri, d) in [
    "energy",      "energy",         "system",       "surroundings",    "physics"
    "charge",      "charge",         "node_in",      "node_out",        "physics"
    "momentum",    "momentum",       "body_a",       "body_b",          "physics"
    "probability", "amplitude",      "preparation",  "measurement",     "physics"
    "geodesic",    "proper_time",    "emission",     "absorption",      "physics"
    "bond",        "principal",      "lender",       "borrower",        "economics"
    "equity",      "residual_claim", "shareholder",  "company",         "economics"
    "repair",      "integrity",      "damage",       "repair_machinery","biology"
    "replication", "license",        "dividing_cell","checkpoint",      "biology"
    "baryon",      "baryon_number",  "matter",       "antimatter",      "physics"
] do insertRow "substrate" "name, resource, ref_left, ref_right, domain"
        (sprintf "'%s', '%s', '%s', '%s', '%s'" n r l ri d)

conn.Close()

printfn ""
printfn "=== config.db ready ==="
printfn "  %s (%d bytes)" dbPath (FileInfo(dbPath).Length)

// Verify
let verify = new SqliteConnection(sprintf "Data Source=%s;Mode=ReadOnly" dbPath)
verify.Open()
let count table =
    use cmd = verify.CreateCommand()
    cmd.CommandText <- sprintf "SELECT COUNT(*) FROM %s" table
    cmd.ExecuteScalar() :?> int64
printfn "  tools:      %d" (count "tool")
printfn "  projects:   %d" (count "project")
printfn "  build keys: %d" (count "build")
printfn "  substrates: %d" (count "substrate")
verify.Close()
