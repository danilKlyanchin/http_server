#pragma once

#include "utils.hpp"
#include <cctype>
#include <utility>
#include <vector>
#include <algorithm>

const std::size_t MAX_BUFFER_SIZE = 16 * 1024;
using HeadersVector = std::vector<std::pair<std::string, std::string>>;

struct HttpRequestLine {
    bool valid = false;
    std::string method;
    std::string path;
    std::string version;
    std::string query_string;

    bool EmptyFieldsExist() const {
        return method.empty() || path.empty() || version.empty();
    }

    bool IsValidVersion() const {
        return version == "HTTP/1.1";
    }

    std::string AsString() const {
        return "method=" + method + " path=" + path + " version=" + version + " query_string=" + query_string;
    }
};

struct ParseHeadersResult {
    bool valid = false;
    HeadersVector data;
};

struct ContentLength {
    bool valid = false;
    std::size_t value = 0;
};

class HttpConnection {
public:
    HttpConnection(MySocket socket)
        : socket_(std::move(socket))
    {
    }

    bool HasTransferEncodingHeader(const HeadersVector& headers) const {
        return std::any_of(headers.begin(), headers.end(), [](const auto& header) {
            return header.first == "transfer-encoding";
        });
    }

    ContentLength GetContentLengthHeader(const HeadersVector& headers, uint64_t client_id) const {
        const auto it = std::find_if(headers.begin(), headers.end(), [](const auto& header) {
            return header.first == "content-length";
        });
        if (it == headers.end()) {
            LogInfo("[client ", client_id, "] content-length header not found");
            return {.valid = true, .value = 0};
        }

        const auto num = std::count_if(headers.begin(), headers.end(), [](const auto& header) {
            return header.first == "content-length";
        });
        if (num > 1) {
            LogError("[client ", client_id, "] content-length duplicate content-length headers");
            return {};
        }

        LogInfo("[client ", client_id, "] content-length: ", it->second);

        const auto& value = it->second;
        if (value.empty()) {
            LogInfo("[client ", client_id, "] content-length is empty");
            return {};
        }

        const std::string digits = "0123456789";
        const auto only_digits = std::all_of(value.begin(), value.end(), [&digits](char ch) {
            return digits.find(ch) != std::string::npos;
        });
        if (!only_digits) {
            LogError("[client ", client_id, "] invalid content-length: ", value);
            return {};
        }

        try {
            return {true, std::stoul(it->second)};
        } catch (const std::exception& e) {
            LogError("[client ", client_id, "] invalid content-length: ", e.what());
            return {};
        }
    }

    ParseHeadersResult ParseHeaders(const std::string& buffer) {
        std::size_t pos = 0;
        const auto request_line_end = buffer.find("\r\n", pos);
        if (request_line_end == std::string::npos) {
            return {};
        }

        pos = request_line_end + 2;
        ParseHeadersResult res{.valid = true};
        while (true) {
            const auto header_end = buffer.find("\r\n", pos);
            const bool is_last_header = header_end == std::string::npos;
            const auto header = is_last_header
                ? buffer.substr(pos)
                : buffer.substr(pos, header_end - pos);
            const auto colon_pos = header.find(":");
            if (colon_pos == std::string::npos) {
                LogError("Invalid header: ", header);
                return {};
            }

            auto name = header.substr(0, colon_pos);
            if (name.empty()) {
                LogError("Invalid header: empty name");
                return {};
            }
            for (char& ch : name) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            auto value = header.substr(colon_pos + 1);
            const auto first = value.find_first_not_of(" \t");
            if (first == std::string::npos) {
                value.clear();
            } else {
                const auto last = value.find_last_not_of(" \t");
                value = value.substr(first, last - first + 1);
            }
            res.data.emplace_back(std::move(name), std::move(value));
            if (is_last_header) {
                break;
            }
            pos = header_end + 2;
        }

        return res;
    }

    bool HasValidHost(const ParseHeadersResult& headers) const {
        std::size_t host_count = 0;
        bool has_value = false;
        for (const auto& [name, value] : headers.data) {
            if (name == "host") {
                ++host_count;
                has_value = !value.empty();
            }
        }
        return headers.valid && host_count == 1 && has_value;
    }

    HttpRequestLine ParseRequestLine(const std::string& buffer) const {
        const auto request_line_end = buffer.find("\r\n");
        if (request_line_end == std::string::npos) {
            return {};
        }

        const auto pos1 = buffer.find(" ");
        const auto pos2 = buffer.find(" ", pos1 + 1);
        if (pos1 == std::string::npos || pos2 == std::string::npos) {
            return {};
        }

        if (pos1 > request_line_end || pos2 > request_line_end) {
            return {};
        }

        const auto full_path = buffer.substr(pos1 + 1, pos2 - pos1 - 1);
        HttpRequestLine res{
            .valid = true,
            .method = buffer.substr(0, pos1),
            .path = GetPath(full_path),
            .version = buffer.substr(pos2 + 1, request_line_end - pos2 - 1),
            .query_string = GetQueryString(full_path),
        };

        if (res.EmptyFieldsExist() || !res.IsValidVersion()) {
            return {};
        }
        return res;
    }

    SendResult Send(const std::string& message) const {
        return socket_.Send(message);
    }

    MessageResult ReceiveHeaders() {
        const std::string header_end = "\r\n\r\n";

        while (message_buffer_.find(header_end) == std::string::npos) {
            if (!CheckLimit(message_buffer_)) {
                return {.code = Code::Error, .error_message = "Buffer limit exceeded"};
            }
            auto result = socket_.ReceiveSome();
            if (result.code != Code::Ok) {
                return result;
            }
            ExpandMessageBuffer(std::move(result.message));
        }

        auto headers = ExtractHeadersFromBuffer();
        if (!CheckHeadersLimit(headers)) {
            return {.code = Code::Error, .error_message = "Headers limit exceeded"};
        }
        return {.code = Code::Ok, .message = std::move(headers)};
    }

private:
    std::string ExtractHeadersFromBuffer() {
        auto headers = message_buffer_.substr(0, message_buffer_.find("\r\n\r\n"));
        message_buffer_ = message_buffer_.substr(message_buffer_.find("\r\n\r\n") + 4);
        return headers;
    }

    void ExpandMessageBuffer(std::string message) {
        message_buffer_ += message;
    }

    bool CheckLimit(const std::string& buffer) const {
        return buffer.size() < MAX_BUFFER_SIZE;
    }

    bool CheckHeadersLimit(const std::string& buffer) const {
        return buffer.size() + 4 <= MAX_BUFFER_SIZE;
    }

    std::string GetPath(const std::string& full_path) const {
        const auto pos = full_path.find("?");
        return pos == std::string::npos ? full_path : full_path.substr(0, pos);
    }

    std::string GetQueryString(const std::string& full_path) const {
        const auto pos = full_path.find("?");
        return pos == std::string::npos ? "" : full_path.substr(pos + 1);
    }

    MySocket socket_;
    std::string message_buffer_;
};
