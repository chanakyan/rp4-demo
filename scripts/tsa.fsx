// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// tsa.fsx — RFC 3161 timestamp token for kernel8.img
//
// Usage:
//   dotnet fsi scripts/tsa.fsx -- build-rpi/kernel8.img
//   cmake --build build-rpi --target tsa

#load "../lib/shell/Shell.fsx"
open Shell

let imgPath =
    match fsi.CommandLineArgs |> Array.tryItem 1 with
    | Some p when exists p -> p
    | Some p -> failwithf "File not found: %s" p
    | None -> failwith "Usage: dotnet fsi tsa.fsx -- <kernel8.img>"

let dir = dirname imgPath
let name = basename imgPath
let hash = sha256 imgPath

printfn "sha256: %s" hash

// Create timestamp request
let tsqPath = $"{dir}/ts_req.tsq"
sh $"openssl ts -query -data {imgPath} -sha256 -no_nonce -out {tsqPath}" |> ignore

// Apple TSA
let appleTsr = $"{dir}/{name}.apple.tsr"
let (rc1, _, _) =
    shFull $"curl -sS -H 'Content-Type: application/timestamp-query' --data-binary @{tsqPath} http://timestamp.apple.com/ts01 -o {appleTsr}"
if rc1 = 0 then printfn "Apple TSA:    %s" appleTsr
else printfn "Apple TSA:    FAILED"

// DigiCert TSA
let digicertTsr = $"{dir}/{name}.digicert.tsr"
let (rc2, _, _) =
    shFull $"curl -sS -H 'Content-Type: application/timestamp-query' --data-binary @{tsqPath} http://timestamp.digicert.com -o {digicertTsr}"
if rc2 = 0 then printfn "DigiCert TSA: %s" digicertTsr
else printfn "DigiCert TSA: FAILED"

// Manifest
let git = sh "git rev-parse --short HEAD"
let now = System.DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")

writeLines $"{dir}/{name}.manifest" [|
    $"sha256: {hash}"
    $"apple_tsr: {name}.apple.tsr"
    $"digicert_tsr: {name}.digicert.tsr"
    $"built: {now}"
    $"git: {git}"
|]
printfn "manifest: %s/%s.manifest" dir name

// Cleanup
if exists tsqPath then System.IO.File.Delete(tsqPath)
