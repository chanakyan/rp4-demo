// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// Doxygen coverage ratchet — coverage can only go up, never down.
// Usage: dotnet fsi ratchet.fsx -- [--update] [--strip-prefix PREFIX] WARN_LOG BASELINE

#load "../../lib/shell/Shell.fsx"
open Shell

let args = fsi.CommandLineArgs |> Array.skip 1 |> Array.toList

let mutable update = false
let mutable stripPrefix = ""
let mutable positional = []

let rec parseArgs = function
    | "--update" :: rest -> update <- true; parseArgs rest
    | "--strip-prefix" :: p :: rest -> stripPrefix <- p; parseArgs rest
    | rest -> positional <- rest

parseArgs args

match positional with
| [ warnLog; baseline ] ->
    if not (exists warnLog) then
        eprintfn "No warnings log found at %s (doxygen not run?)" warnLog
        exit 1

    // Count warnings per file
    let current =
        lines warnLog
        |> Array.map (fun l -> let parts = l.Split(':', 3) in if parts.Length >= 2 then parts.[0] else "")
        |> Array.filter (fun f -> f <> "")
        |> Array.map (fun f -> if stripPrefix <> "" && f.StartsWith(stripPrefix) then f.[stripPrefix.Length..] else f)
        |> freq

    if update then
        current
        |> Array.sortBy fst
        |> Array.map (fun (f, n) -> $"{f}:{n}")
        |> writeLines baseline
        printfn "Baseline updated: %d files." current.Length
        exit 0

    if not (exists baseline) then
        eprintfn "No baseline at %s. Run with --update to create." baseline
        exit 1

    let baseCounts =
        lines baseline
        |> Array.choose (fun l ->
            let parts = l.Split(':', 2)
            if parts.Length = 2 then
                match System.Int32.TryParse(parts.[1]) with
                | true, n -> Some (parts.[0], n)
                | _ -> None
            else None)
        |> dict

    let mutable fail = false
    for (f, actual) in current do
        let allowed = if baseCounts.ContainsKey(f) then baseCounts.[f] else 0
        if actual > allowed then
            printfn "REGRESSED: %s — %d undocumented (baseline: %d)" f actual allowed
            fail <- true

    if fail then exit 1
    printfn "Doc coverage: no regressions."

| _ ->
    eprintfn "Usage: dotnet fsi ratchet.fsx -- [--update] [--strip-prefix P] WARN_LOG BASELINE"
    exit 1
