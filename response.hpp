#pragma once

#include <string>
#include <fmt/format.h>

class HttpResponse {
public:
    HttpResponse(std::size_t status_code, const std::string& status_message, const std::string& body, const std::string& initial_headers = "")
        : status_code_(status_code)
        , status_message_(status_message)
        , headers_(initial_headers)
        , body_(body)
    {
        PrepareBaseHeaders();
    }

    std::string ToString() const {
        return fmt::format("HTTP/1.1 {} {}\r\n{}\r\n{}", status_code_, status_message_, headers_, body_);
    }

    static HttpResponse BadRequestResponse(std::string initial_headers = "") {
        initial_headers += "Connection: close\r\n";
        return HttpResponse(400, "Bad Request", "Bad request", initial_headers);
    }

    static HttpResponse NotFoundResponse(std::string initial_headers = "") {
        return HttpResponse(404, "Not Found", "Not found", initial_headers);
    }

    static HttpResponse NotAllowedResponse(std::string initial_headers = "") {
        initial_headers += "Allow: GET\r\n";
        return HttpResponse(405, "Method Not Allowed", "Method Not Allowed", initial_headers);
    }

private:
    void PrepareBaseHeaders() {
        headers_ += "Content-Type: text/plain; charset=utf-8\r\n";
        headers_ += "Content-Length: " + std::to_string(body_.size()) + "\r\n";
    }

    std::size_t status_code_;
    std::string status_message_;
    std::string headers_;
    std::string body_;
};
