// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// configsrv — HTTP config server
//
// All configuration comes from config.db (build table):
//   configsrv_port  — listen port
//   configsrv_dist  — static file directory
//
// Serves:
//   GET  /              -> dist/index.html
//   GET  /*             -> static files from configsrv_dist
//   GET  /api/config    -> all config keys as JSON
//   GET  /api/config/:k -> single config value as JSON
//   PUT  /api/config/:k -> update config value (JSON body: {"value": ...})
//
// Usage: ./configsrv [config.db]

#include <sqlite3.h>
#include <iostream>
#include <string>

import rp4.http;

namespace {

// ─── Config loaded from config.db at startup ────────────────────────────────

struct server_config {
    std::string db_path;
    std::string dist_dir;
    int port;
};

auto read_build_key(sqlite3* db, char const* key, char const* fallback)
    -> std::string
{
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT value FROM build WHERE key = ?",
                       -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    std::string result = fallback;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = reinterpret_cast<char const*>(sqlite3_column_text(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return result;
}

auto load_config(std::string const& db_path) -> server_config {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr)
        != SQLITE_OK) {
        std::cerr << "configsrv: cannot open " << db_path << "\n";
        std::exit(1);
    }
    server_config cfg;
    cfg.db_path  = db_path;
    cfg.port     = std::stoi(read_build_key(db, "configsrv_port", "8080"));
    cfg.dist_dir = read_build_key(db, "configsrv_dist", "dist");
    sqlite3_close(db);
    return cfg;
}

// ─── MIME types ─────────────────────────────────────────────────────────────

auto content_type(std::string const& ext) -> std::string {
    if (ext == ".html") return "text/html";
    if (ext == ".js")   return "application/javascript";
    if (ext == ".css")  return "text/css";
    if (ext == ".json") return "application/json";
    if (ext == ".svg")  return "image/svg+xml";
    if (ext == ".png")  return "image/png";
    if (ext == ".ico")  return "image/x-icon";
    return "application/octet-stream";
}

// ─── SQLite helpers for config API ──────────────────────────────────────────

auto db_get_all(std::string const& db_path) -> rp4::http::response {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr)
        != SQLITE_OK) {
        return {.status = 500, .body = R"({"error":"cannot open config.db"})"};
    }

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT key, value FROM build ORDER BY key",
                       -1, &stmt, nullptr);
    std::string out = "[";
    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) out += ",";
        first = false;
        auto k = reinterpret_cast<char const*>(sqlite3_column_text(stmt, 0));
        auto v = reinterpret_cast<char const*>(sqlite3_column_text(stmt, 1));
        out += R"({"key":")";
        out += k;
        out += R"(","value":")";
        out += v;
        out += R"("})";
    }
    out += "]";
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return {.status = 200, .body = std::move(out)};
}

auto db_get_key(std::string const& db_path, std::string const& key)
    -> rp4::http::response
{
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr)
        != SQLITE_OK) {
        return {.status = 500, .body = R"({"error":"cannot open config.db"})"};
    }

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT value FROM build WHERE key = ?",
                       -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);

    rp4::http::response resp;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        auto v = reinterpret_cast<char const*>(sqlite3_column_text(stmt, 0));
        resp = {.status = 200,
                .body = R"({"key":")" + key + R"(","value":")" + v + R"("})"};
    } else {
        resp = {.status = 404, .body = R"({"error":"key not found"})"};
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return resp;
}

auto db_put_key(std::string const& db_path, std::string const& key,
                std::string const& json_body) -> rp4::http::response
{
    auto value = rp4::http::json_parse_value(json_body, "value");

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr)
        != SQLITE_OK) {
        return {.status = 500, .body = R"({"error":"cannot open config.db"})"};
    }

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "UPDATE build SET value = ? WHERE key = ?",
                       -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, value.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, key.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);

    int const changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (changes > 0) {
        return {.status = 200,
                .body = R"({"key":")" + key + R"(","value":")" + value
                        + R"(","updated":true})"};
    }
    return {.status = 404, .body = R"({"error":"key not found"})"};
}

} // namespace

// ─── Main ──────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    std::string const db = (argc > 1) ? argv[1] : "config.db";
    auto const cfg = load_config(db);

    std::cout << "configsrv:\n";
    std::cout << "  db:   " << cfg.db_path << "\n";
    std::cout << "  dist: " << cfg.dist_dir << "\n";

    auto const route = [&cfg](rp4::http::request const& req)
        -> rp4::http::response
    {
        auto const& uri = req.uri;

        // ── Config API ────────────────────────────────────────────────
        if (uri.starts_with("/api/config")) {
            std::string key;
            if (uri.size() > 12 && uri[11] == '/') {
                key = uri.substr(12);
            }

            if (req.method == "GET") {
                return key.empty() ? db_get_all(cfg.db_path)
                                   : db_get_key(cfg.db_path, key);
            }
            if (req.method == "PUT") {
                if (key.empty()) {
                    return {.status = 400,
                            .body = R"({"error":"key required"})"};
                }
                return db_put_key(cfg.db_path, key, req.body);
            }
            return {.status = 405,
                    .body = R"({"error":"method not allowed"})"};
        }

        // ── Static files ──────────────────────────────────────────────
        std::string path = (uri == "/" || uri == "/index.html")
            ? cfg.dist_dir + "/index.html"
            : cfg.dist_dir + uri;

        if (!rp4::http::file_exists(path)) {
            return {.status = 404,
                    .content_type = "text/plain",
                    .body = "404 Not Found"};
        }

        auto const ext = rp4::http::file_extension(path);
        return {.status = 200,
                .content_type = content_type(ext),
                .file_path = std::move(path)};
    };

    rp4::http::server srv({.port = cfg.port, .route = route});
    srv.wait_for_shutdown();
    return 0;
}
