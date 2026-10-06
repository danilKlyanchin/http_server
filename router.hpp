#pragma once

#include "response.hpp"

class Router{
public:
    Router() = default;
    ~Router() = default;
    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;

    HttpResponse GetResponse(const HttpRequestLine& request_line) {
        if (!IsKnownPath(request_line.path)) {
            return HttpResponse::NotFoundResponse();
        }
        if (request_line.method != "GET") {
            return HttpResponse::NotAllowedResponse();
        }
        return HttpResponse(200, "OK", GetBodyPyPath(request_line.path));
    }

private:
    bool IsKnownPath(const std::string& path) {
        return path == "/" || path == "/health";
    }

    std::string GetBodyPyPath(const std::string& path) {
        if (path == "/") {
            return "Hello, HTTP!";
        }
        if (path == "/health") {
            return "My health is ok";
        }
        throw std::runtime_error("Unknown path");
    }
};
