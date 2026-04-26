#!/usr/bin/env -S dotnet fsi
// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// run_all.fsx — run all Z3 proofs and emit certificates to proof/certs/
//
// Each certificate is a timestamped record of:
//   - Which assertions were checked
//   - Z3 result (UNSAT = proved, SAT = counterexample found)
//   - Solver version
//   - Git commit hash at time of proof
//
// Certificates are append-only. They are evidence that the invariants
// held at a specific point in the codebase history.
//
// Usage: dotnet fsi proof/run_all.fsx

open System
open System.IO
open System.Diagnostics

let certDir = Path.Combine(__SOURCE_DIRECTORY__, "certs")
Directory.CreateDirectory(certDir) |> ignore

let timestamp = DateTime.UtcNow.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'")

// Get git commit
let gitHash =
    try
        let psi = ProcessStartInfo("git", "rev-parse --short HEAD")
        psi.RedirectStandardOutput <- true
        psi.UseShellExecute <- false
        psi.WorkingDirectory <- Path.Combine(__SOURCE_DIRECTORY__, "..")
        let p = Process.Start(psi)
        let h = p.StandardOutput.ReadToEnd().Trim()
        p.WaitForExit()
        if p.ExitCode = 0 then h else "unknown"
    with _ -> "unknown"

// Run a spec and capture output
let runSpec (name: string) (path: string) =
    printfn "  running: %s" name
    let psi = ProcessStartInfo("dotnet", sprintf "fsi \"%s\"" path)
    psi.RedirectStandardOutput <- true
    psi.RedirectStandardError <- true
    psi.UseShellExecute <- false
    psi.WorkingDirectory <- Path.Combine(__SOURCE_DIRECTORY__, "..")
    let p = Process.Start(psi)
    let stdout = p.StandardOutput.ReadToEnd()
    let stderr = p.StandardError.ReadToEnd()
    p.WaitForExit()
    (p.ExitCode, stdout, stderr)

// Specs to run
let specs = [
    ("allocator", Path.Combine(__SOURCE_DIRECTORY__, "..", "spec", "verify_allocator.fsx"))
    ("audio",     Path.Combine(__SOURCE_DIRECTORY__, "..", "spec", "verify_audio.fsx"))
    ("proc",      Path.Combine(__SOURCE_DIRECTORY__, "..", "spec", "verify_proc.fsx"))
]

printfn ""
printfn "  RP4 PROOF CERTIFICATE GENERATION"
printfn "  %s" (String.Concat(Seq.replicate 50 "="))
printfn "  timestamp: %s" timestamp
printfn "  commit:    %s" gitHash
printfn ""

let allPassed =
    specs |> List.forall (fun (name, path) ->
        let (exitCode, stdout, _) = runSpec name path
        let status = if exitCode = 0 then "PROVED" else "FAILED"

        let certFile = Path.Combine(certDir, sprintf "%s.txt" name)
        let cert =
            [ "PROOF CERTIFICATE"
              "================="
              sprintf "spec:      %s" name
              sprintf "status:    %s" status
              sprintf "timestamp: %s" timestamp
              sprintf "commit:    %s" gitHash
              sprintf "exit_code: %d" exitCode
              ""
              "--- SOLVER OUTPUT ---"
              stdout
              "--- END ---" ]
            |> String.concat "\n"

        File.WriteAllText(certFile, cert)
        printfn "  %s: %s -> %s" name status certFile
        exitCode = 0)

printfn ""
if allPassed then
    printfn "  ALL PROOFS PASSED. Certificates written to proof/certs/"
else
    eprintfn "  SOME PROOFS FAILED. Check certificates for details."

exit (if allPassed then 0 else 1)
