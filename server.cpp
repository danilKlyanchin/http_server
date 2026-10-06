#include "connection.hpp"
#include "constants.hpp"
#include "logging.hpp"
#include "response.hpp"
#include "utils.hpp"
#include <arpa/inet.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <semaphore>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include "router.hpp"

using ClientSlots = std::counting_semaphore<MAX_CONCURRENT_CLIENTS>;

class ClientSlotGuard {
public:
    explicit ClientSlotGuard(std::shared_ptr<ClientSlots> client_slots)
        : client_slots_(std::move(client_slots))
    {
    }

    ClientSlotGuard(const ClientSlotGuard&) = delete;
    ClientSlotGuard& operator=(const ClientSlotGuard&) = delete;

    ~ClientSlotGuard() {
        client_slots_->release();
    }

private:
    std::shared_ptr<ClientSlots> client_slots_;
};

void LogHeaders(const HeadersVector& headers, uint64_t client_id) {
    LogInfo("[client ", client_id, "] received headers:");
    for (const auto& [key, value]: headers) {
        LogInfo("[client ", client_id, "] header: ", key, "=", value);
    }
}

void HandleClient(MySocket client_socket, uint64_t client_id) {
    LogInfo("[client ", client_id, "] connection accepted");

    Router router;
    HttpConnection client_connection(std::move(client_socket));

    LogInfo("[client ", client_id, "] waiting for message");
    const auto result = client_connection.ReceiveHeaders();
    if (result.code == Code::Error) {
        LogError("[client ", client_id, "] failed to receive headers: ", result.error_message);
        return;
    }
    if (result.code == Code::Disconnected) {
        LogInfo("[client ", client_id, "] disconnected");
        return;
    }

    LogInfo("[client ", client_id, "] received headers bytes=", result.message.size());
    LogInfo("[client ", client_id, "] received headers:\n", result.message);

    const auto parsed_headers = client_connection.ParseHeaders(result.message);
    if (!parsed_headers.valid) {
        LogError("[client ", client_id, "] invalid headers");
        client_connection.SendBadResponse(client_id);
        return;
    }

    LogHeaders(parsed_headers.data, client_id);
    const auto content_length_result = client_connection.GetContentLengthHeader(parsed_headers.data, client_id);
    if (!content_length_result.valid) {
        LogError("[client ", client_id, "] invalid content-length header");
        client_connection.SendBadResponse(client_id);
        return;
    }

    if (content_length_result.value != 0) {
        LogError("[client ", client_id, "] request body is not supported");
        client_connection.SendBadResponse(client_id);
        return;
    }

    if (client_connection.HasTransferEncodingHeader(parsed_headers.data)) {
        LogError("[client ", client_id, "] transfer-encoding is not supported");
        client_connection.SendBadResponse(client_id);
        return;
    }

    if (!client_connection.HasValidHost(parsed_headers)) {
        LogError("[client ", client_id, "] missing, empty or duplicate Host header");
        client_connection.SendBadResponse(client_id);
        return;
    }

    const auto request_line = client_connection.ParseRequestLine(result.message);
    if (!request_line.valid) {
        LogError("[client ", client_id, "] invalid request line");
        client_connection.SendBadResponse(client_id);
        return;
    }
    LogInfo("[client ", client_id, "] request line: ", request_line.AsString());

    const auto do_close = client_connection.HasCloseConnectionHeader(parsed_headers.data);
    LogInfo("[client ", client_id, "] connection close requested: ", do_close);

    const auto prepared_response = router.GetResponse(request_line).ToString();
    auto send_result = client_connection.Send(prepared_response);
    if (send_result.code == Code::Error) {
        LogError("[client ", client_id, "] send failed: ", send_result.error_message);
        return;
    }

    LogInfo("[client ", client_id, "] sent response response bytes=", prepared_response.size());
}

void HandleClientSafely(MySocket client_socket,
                        std::shared_ptr<ClientSlots> client_slots,
                        uint64_t client_id) noexcept {
    ClientSlotGuard client_slot_guard(std::move(client_slots));
    try {
        HandleClient(std::move(client_socket), client_id);
    } catch (const std::exception& e) {
        LogError("[client ", client_id, "] handler failed: ", e.what());
    } catch (...) {
        LogError("[client ", client_id,
                 "] handler failed with an unknown exception");
    }
}

int RunServer() {
    in_addr server_in_addr;
    if (!handle_inet_pton(SERVER_ADDRESS, server_in_addr)) {
        return EXIT_FAILURE;
    }

    auto server_socket = MySocket();
    int bind_return_code = server_socket.Bind(SERVER_PORT, server_in_addr);
    if (bind_return_code != 0) {
        LogError("[server] bind failed: ", std::strerror(errno));
        return EXIT_FAILURE;
    }

    int listen_return_code =
        listen(server_socket.GetSocket(), MAX_CONCURRENT_CLIENTS);
    if (listen_return_code != 0) {
        LogError("[server] listen failed: ", std::strerror(errno));
        return EXIT_FAILURE;
    }

    LogInfo("[server] listening on port ", SERVER_PORT);
    auto client_slots = std::make_shared<ClientSlots>(MAX_CONCURRENT_CLIENTS);
    uint64_t next_client_id = 1;
    while (true) {
        LogInfo("[server] waiting for connection");
        int accepted_socket_fd =
            accept(server_socket.GetSocket(), nullptr, nullptr);
        if (accepted_socket_fd == -1 && errno == EINTR) {
            continue;
        }
        if (accepted_socket_fd == -1) {
            LogError("[server] accept failed: ", std::strerror(errno));
            continue;
        }

        const uint64_t client_id = next_client_id++;
        bool client_slot_acquired = false;
        try {
            MySocket client_socket(accepted_socket_fd);
            if (!client_slots->try_acquire()) {
                LogError("[client ", client_id, "] rejected: too many clients");
                continue;
            }
            client_slot_acquired = true;

            std::thread client_thread(HandleClientSafely, std::move(client_socket),
                                      client_slots, client_id);
            client_slot_acquired = false;
            client_thread.detach();
        } catch (const std::exception& e) {
            if (client_slot_acquired) {
                client_slots->release();
            }
            LogError("[client ", client_id,
                     "] failed to start handler thread: ", e.what());
        }
    }

    return 0;
}

int main() {
    try {
        return RunServer();
    } catch (const std::exception& e) {
        LogError("[server] fatal error: ", e.what());
        return EXIT_FAILURE;
    }
}
