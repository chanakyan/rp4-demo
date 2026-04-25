// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// httpmgr — HTTP resource manager (lwIP httpd → PPS bridge)
//
// Serves dist/ from memfs (index.html, app.js, style.css).
// Routes /api/pps/* to kernel PPS read/publish.
// Routes /api/memfs/* to memfsmgr capabilities.
//
// The phone browser talks to this. Nothing else.
// Runs on bare-metal via lwIP (Ethernet or wifi via firmware).
//
// Subscribes to:
//   /pps/cmd/httpstart → start HTTP server on configured port
//
// Publishes:
//   /pps/http/status   → listening, port, connections

import std;
import qnx.kernel;
import rp4.hal;
import rp4.config;

using namespace qnx;
using namespace qnx::kernel;
using namespace qnx::pps;
using namespace rp4::hal;

// ─── lwIP httpd callbacks (C linkage) ──────────────────────────────────────
// lwIP httpd uses CGI handlers for dynamic content.
// We register handlers for /api/* routes.

extern "C" {
    // These will be implemented when lwIP is wired to the BCM2711
    // Ethernet MAC (GENET) or USB wifi. For now, stubs.

    // lwIP httpd CGI handler signature:
    // const char* cgi_handler(int index, int num_params,
    //                         char *params[], char *values[]);

    // Custom FS hook: serve files from memfs instead of compiled-in fsdata
    // int fs_open_custom(struct fs_file *file, const char *name);
}

// ─── PPS ↔ HTTP bridge ────────────────────────────────────────────────────
// Maps HTTP routes to PPS operations:
//
//   GET  /api/pps/<path>        → pps.read_object(path), return JSON
//   PUT  /api/pps/cmd/<key>     → pps.publish("/pps/cmd/<key>", "value", body)
//   POST /api/reboot/kodi       → reboot into tryboot (Linux/Kodi)
//   POST /api/reboot/kernel     → normal reboot (microkernel)
//   GET  /api/memfs/<name>      → read capability from memfsmgr, return bytes
//   GET  /*                     → serve from dist/ (index.html etc.)

static Kernel* g_kernel = nullptr;

// Called by lwIP httpd CGI for /api/pps/* GET requests
static auto handle_pps_get(std::string_view path) -> std::string {
    if (!g_kernel) return R"({"error":"no kernel"})";
    auto& pps = g_kernel->pps();

    // Read all attributes of the PPS object
    auto attrs = pps.read_object(path);
    if (!attrs) return R"({"error":"not found"})";

    std::string json = "{";
    bool first = true;
    for (auto& a : *attrs) {
        if (!first) json += ',';
        first = false;
        json += '"';
        json += a.key;
        json += R"(":)";
        json += '"';
        json += a.value;
        json += '"';
    }
    json += '}';
    return json;
}

// Called by lwIP httpd CGI for /api/pps/cmd/* PUT requests
static auto handle_pps_put(std::string_view key, std::string_view value) -> std::string {
    if (!g_kernel) return R"({"error":"no kernel"})";
    auto& pps = g_kernel->pps();

    std::string path = "/pps/cmd/";
    path += key;
    (void)pps.publish(path, "value", value);

    std::string json = R"({"key":")";
    json += key;
    json += R"(","value":")";
    json += value;
    json += R"(","ok":true})";
    return json;
}

// Called by lwIP httpd CGI for /api/reboot/* POST requests
static auto handle_reboot(std::string_view target) -> std::string {
    if (target == "kodi") {
        uart::puts("httpmgr: reboot to Kodi requested\n");
        rp4::hal::reboot::to_kodi();  // does not return
    }
    if (target == "kernel") {
        uart::puts("httpmgr: reboot to microkernel requested\n");
        rp4::hal::reboot::to_microkernel();  // does not return
    }
    return R"({"error":"unknown target, use kodi or kernel"})";
}

// ─── Entry ─────────────────────────────────────────────────────────────────

auto httpmgr_main(Kernel& k) -> void {
    g_kernel = &k;
    auto& pps = k.pps();

    (void)pps.publish("/pps/http/status", "state", "init");
    (void)pps.publish("/pps/http/status", "port",
        std::to_string(rp4::config::configsrv_port));

    constexpr auto pid = ProcessId{14};

    // Start HTTP server when commanded (or at boot)
    (void)pps.subscribe("/pps/cmd/httpstart", pid, [&](const Notification&) {
        // TODO: Initialize lwIP stack:
        //   1. BCM2711 GENET (Ethernet MAC) driver
        //   2. lwip_init()
        //   3. netif_add() with GENET driver
        //   4. dhcp_start() for IP
        //   5. mdns_resp_init() + mdns_resp_add_netif() → "rp4.local"
        //   6. httpd_init() to start listening on port 80
        //   7. Register CGI handlers for /api/*
        //   8. Register custom FS hook to serve dist/ from memfs
        //
        // mDNS: phone types "rp4.local" in Safari. Zero-conf, no IP needed.
        // lwIP mdns is in vendor/lwip/src/apps/mdns/ (BSD).
        //
        // Until GENET driver is written, this is a stub.
        (void)pps.publish("/pps/http/status", "state", "waiting for GENET driver");
        (void)pps.publish("/pps/http/status", "hostname", "rp4.local");
        uart::puts("httpmgr: lwIP httpd + mDNS needs GENET driver\n");
    });

    // Auto-start
    (void)pps.publish("/pps/cmd/httpstart", "value", "1");

    // Cooperative: callbacks fire synchronously.
}

int main() {
    Kernel k;
    k.init();
    std::println("=== rp4 httpmgr (host build) ===");
    std::println("  lwIP httpd → PPS bridge");
    std::println("  serves dist/ + /api/pps/* + /api/memfs/*");
    return 0;
}
