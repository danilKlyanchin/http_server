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
        PrepareHeaders();
    }

    std::string ToString() const {
        return fmt::format("HTTP/1.1 {} {}\r\n{}\r\n{}", status_code_, status_message_, headers_, body_);
    }

    static HttpResponse BadRequestResponse() {
        return HttpResponse(400, "Bad Request", "Bad request");
    }

    static HttpResponse NotFoundResponse() {
        return HttpResponse(404, "Not Found", "Not found");
    }

    static HttpResponse NotAllowedResponse() {
        return HttpResponse(405, "Method not allowed", "Method not allowed", "Allow: GET\r\n");
    }

private:
    void PrepareHeaders() {
        headers_ += "Content-Type: text/plain; charset=utf-8\r\n";
        headers_ += "Connection: close\r\n";
        headers_ += "Content-Length: " + std::to_string(body_.size()) + "\r\n";
    }

    std::size_t status_code_;
    std::string status_message_;
    std::string headers_;
    std::string body_;
};
