#pragma once

#include "response.hpp"
#include "connection.hpp"

class Router{
public:
    Router(bool client_has_close_header)
        : client_has_close_header_(client_has_close_header)
        {};

    HttpResponse GetResponse(const HttpRequestLine& request_line) {
        if (!IsKnownPath(request_line.path)) {
            return HttpResponse::NotFoundResponse(GetAdditionalHeaders());
        }
        if (request_line.method != "GET") {
            return HttpResponse::NotAllowedResponse(GetAdditionalHeaders());
        }
        return HttpResponse(
            200,
            "OK",
            GetBodyPyPath(request_line.path),
            GetAdditionalHeaders()
        );
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

    std::string GetAdditionalHeaders() {
        if (client_has_close_header_) {
            return "Connection: close\r\n";
        }
        return "";
    }

    const bool client_has_close_header_;
};
