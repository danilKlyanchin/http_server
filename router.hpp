#pragma once


class Router{
public:
    Router() = default;
    ~Router() = default;
    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;

    std::string GetResponse(const HttpRequestLine& request_line) {
        CheckKnownPath(request_line.path);
        PrepareStatusCodeAndMessage(request_line.method);
        PrepareBody(request_line.path);


        const auto headers = PrepareHeaders();
        return headers + "\r\n" + body_;
    }

    std::string GetBadRequestResponse() {
        status_code_ = 400;
        status_msg_ = "Bad Request";
        additional_header_.clear();
        body_ = "Bad request";
        return PrepareHeaders() + "\r\n" + body_;
    }

private:
    void CheckKnownPath(const std::string& path) {
        known_path_ = (path == "/" || path == "/health");
    }

    void PrepareStatusCodeAndMessage(const std::string& method) {
        if (!known_path_) {
            status_code_ = 404;
            status_msg_ = "Not Found";
            additional_header_ = "";
        } else if (method != "GET") {
            status_code_ = 405;
            status_msg_ = "Method not allowed";
            additional_header_ = "Allow: GET\r\n";
        } else {
            status_code_ = 200;
            status_msg_ = "OK";
            additional_header_ = "";
        }
    }

    void PrepareBody(const std::string& path) {
        if (status_code_ == 404) {
            body_ = "Not found";
        } else if (status_code_ == 405) {
            body_ = "Method not allowed";
        } else {
            body_ = GetBodyPyPath(path);
        }
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

    std::string PrepareHeaders() {
        return "HTTP/1.1 " + std::to_string(status_code_) + " " + status_msg_ + "\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Connection: close\r\n"
            "Content-Length: " + std::to_string(body_.size()) + "\r\n" +\
            additional_header_;
    }

    bool known_path_;
    std::size_t status_code_;
    std::string status_msg_;
    std::string additional_header_;
    std::string body_;
};
