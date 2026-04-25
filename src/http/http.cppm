// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// rp4.http — Jail module for Poco HTTP server.
//
// All Poco #includes are confined here. App code sees only the exported
// value types and rp4::http::server. No raw pointers, no new/delete,
// no polymorphic handler classes leak out.

module;

// ── Poco (jailed) ──────────────────────────────────────────────────────────
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Util/ServerApplication.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/StreamCopier.h>
#include <Poco/Path.h>
#include <Poco/File.h>

// ── stdlib (jailed — configsrv needs these before import std works with Poco) ─
#include <csignal>
#include <iostream>
#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <sstream>

export module rp4.http;

// ─── Exported value types ──────────────────────────────────────────────────

export namespace rp4::http {

/** @brief Incoming HTTP request (value type, no Poco dependencies). */
struct request {
    std::string method;  ///< HTTP method (GET, PUT, POST, etc.)
    std::string uri;     ///< Request URI path.
    std::string body;    ///< Request body (read eagerly for PUT/POST).
};

/** @brief Outgoing HTTP response (value type, no Poco dependencies). */
struct response {
    int         status       = 200;                ///< HTTP status code.
    std::string content_type = "application/json"; ///< Content-Type header.
    std::string body;                              ///< Response body text.
    std::string file_path;   ///< Non-empty: send file instead of body.
};

/** @brief Route handler: pure function from request to response. */
using handler = std::function<response(request const&)>;

/** @brief Configuration for an HTTP server instance. */
struct server_config {
    int     port = 8080; ///< TCP listen port.
    handler route;       ///< Single dispatch function for all requests.
};

// ─── JSON helpers (thin wrappers over Poco::JSON, jailed here) ─────────

/** @brief Extract a string value from a JSON object by key. */
auto json_parse_value(std::string const& json_body, std::string const& key)
    -> std::string
{
    Poco::JSON::Parser parser;
    auto parsed = parser.parse(json_body);
    auto obj = parsed.extract<Poco::JSON::Object::Ptr>();
    return obj->getValue<std::string>(key);
}

// ─── File helpers (thin wrappers over Poco::File/Path) ─────────────────

/** @brief Check whether a file exists at the given path. */
auto file_exists(std::string const& path) -> bool {
    return Poco::File(path).exists();
}

/** @brief Return the file extension including the leading dot. */
auto file_extension(std::string const& path) -> std::string {
    return "." + Poco::Path(path).getExtension();
}

// ─── Server (RAII — starts on construct, stops on destruct) ────────────

/** @brief RAII HTTP server. Starts on construction, stops on destruction. */
class server {
public:
    /** @brief Construct and start the server with the given config. */
    explicit server(server_config cfg);
    /** @brief Stop the server. */
    ~server();

    server(server const&)            = delete;
    server& operator=(server const&) = delete;
    server(server&&)                 = delete;
    server& operator=(server&&)      = delete;

    /** @brief Block until SIGINT/SIGTERM. */
    void wait_for_shutdown();

private:
    server_config                                  cfg_;
    std::unique_ptr<Poco::Net::ServerSocket>       socket_;
    std::unique_ptr<Poco::Net::HTTPServer>         http_;
};

} // namespace rp4::http

// ─── Implementation (not exported — Poco types stay here) ──────────────

namespace rp4::http {

namespace {

class bridge_handler : public Poco::Net::HTTPRequestHandler {
public:
    explicit bridge_handler(handler const& fn) : fn_(fn) {}

    void handleRequest(Poco::Net::HTTPServerRequest& req,
                       Poco::Net::HTTPServerResponse& resp) override
    {
        // Read request into value type
        request r;
        r.method = req.getMethod();
        r.uri    = req.getURI();
        if (r.method == "PUT" || r.method == "POST") {
            Poco::StreamCopier::copyToString(req.stream(), r.body);
        }

        // Dispatch to user's pure function
        auto result = fn_(r);

        // Write response
        resp.setStatusAndReason(
            static_cast<Poco::Net::HTTPResponse::HTTPStatus>(result.status));
        resp.setContentType(result.content_type);

        if (!result.file_path.empty()) {
            resp.sendFile(result.file_path, result.content_type);
        } else {
            resp.send() << result.body;
        }
    }

private:
    handler const& fn_;
};

class bridge_factory : public Poco::Net::HTTPRequestHandlerFactory {
public:
    explicit bridge_factory(handler const& fn) : fn_(fn) {}

    auto createRequestHandler(const Poco::Net::HTTPServerRequest&)
        -> Poco::Net::HTTPRequestHandler* override
    {
        return new bridge_handler(fn_);
    }

private:
    handler const& fn_;
};

} // anon namespace

server::server(server_config cfg)
    : cfg_(std::move(cfg))
    , socket_(std::make_unique<Poco::Net::ServerSocket>(cfg_.port))
    , http_(std::make_unique<Poco::Net::HTTPServer>(
          new bridge_factory(cfg_.route), *socket_,
          new Poco::Net::HTTPServerParams))
{
    http_->start();
    std::cout << "rp4::http::server listening on port " << cfg_.port << "\n";
}

server::~server() {
    http_->stop();
}

void server::wait_for_shutdown() {
    // Simple signal-based wait: Poco's ServerApplication does this internally
    // but we don't want to force inheritance. Use pause() + signal handler.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    int sig = 0;
    sigwait(&mask, &sig);
}

} // namespace rp4::http
