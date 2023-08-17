#include "../../ppchat-shared/include/ppchat_shared.h"

#include <stdlib.h>

bool g_quit = false;

DWORD CALLBACK listen_for_incoming_network_data(void *context) {
    SocketContext *ctx = static_cast<SocketContext *>(context);

    // Store the socket handle value locally for now.
    Socket socket = *ctx->socket;

    if (socket.handle == INVALID_SOCKET) {
        log_error("Couldn't listen for incoming network data because connection socket was invalid.");
        return EXIT_FAILURE;
    }

    char receive_buffer[PPCHAT_RECEIVE_BUFFER_SIZE + 1];
    size_t receive_buffer_size = sizeof(receive_buffer);
    int bytes_received = 0;
    do {
        memset(receive_buffer, 0, receive_buffer_size);
        bytes_received = ppchat_receive(socket, receive_buffer, receive_buffer_size - 1, NULL);
        if (bytes_received == SOCKET_ERROR) {

            /* An error occured while receiving network data. */

            int error = get_last_socket_error();
            switch (error) {
                case WSAECONNRESET: {
                    log("Connection with '%s' has been abruptly closed by remote peer.", ctx->client_ip);
                    break;
                };
                case WSAECONNABORTED: {
                    log("Connection with '%s' has been aborted by a local software problem.", ctx->client_ip);
                    break;
                };
                default: {
                    log_error("Couldn't receive network data. Error: %d - %s", error, get_error_description(error));
                };
            }

            ppchat_close_socket(&socket);

        } else if (bytes_received == 0) {

            /* Connection was gratefully closed. */

            log("Connection with '%s' has been closed.", ctx->client_ip);
            ppchat_close_socket(&socket);

        } else {

            /* Network data received. */

            // Null-terminate string.
            int last_character_index = clamp(0, receive_buffer_size-1, bytes_received);
            receive_buffer[last_character_index] = '\0';

            log("Received %d bytes from '%s'. Message: \"%s\"", bytes_received, ctx->client_ip, receive_buffer);

            // Echo back.
            int send_result = ppchat_send(socket, receive_buffer, bytes_received, 0);
            if (send_result == SOCKET_ERROR) {
                int error = get_last_socket_error();
                log_error("Couldn't send message to '%s'. Error: %d - %s", ctx->client_ip, error, get_error_description(error));
                ppchat_close_socket(&socket);
            }

        }
    } while (bytes_received > 0 && !g_quit);
}

DWORD CALLBACK listen_for_incoming_connections(void *context) {
    (void)context;

    addrinfo hints = { };

    // ai - address info.
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;
    
    addrinfo *server;
    int server_address_info_result = ppchat_getaddrinfo(
        /* Node name (IP)      */ NULL,
        /* Service name (port) */ PPCHAT_DEFAULT_PORT,
        /* Address info hints  */ &hints, // Includes: address family, socket type, protocol.
        /* Result array        */ &server
    );
    if (server_address_info_result != 0) {
        exit_with_error("Couldn't get server address info. Error: %d - %s", server_address_info_result, get_error_description(server_address_info_result));
    }

    Socket listen_socket = ppchat_create_socket(server->ai_family, server->ai_socktype, server->ai_protocol);
    if (listen_socket.handle == INVALID_SOCKET) {
        int error = get_last_socket_error();
        exit_with_error("Couldn't create listen socket. Error: %d - %s", error, get_error_description(error));
    }

    int bind_result = ppchat_bind(listen_socket, server->ai_addr, (int) server->ai_addrlen);
    if (bind_result == SOCKET_ERROR) {
        int error = get_last_socket_error();
        exit_with_error("Couldn't bind listen socket. Error: %d - %s", error, get_error_description(error));
    }

    ppchat_freeaddrinfo(server);

    int listen_result = ppchat_listen(listen_socket, SOMAXCONN);
    if (listen_result == SOCKET_ERROR) {
        int error = get_last_socket_error();
        exit_with_error("Couldn't listen on listen socket. Error: %d - %s", error, get_error_description(error));
    }


    while (!g_quit) {
        sockaddr_in client_address = { };
        int client_address_size = sizeof(client_address);
        Socket client_socket = ppchat_accept(listen_socket, (sockaddr *) &client_address, &client_address_size);
        if (client_socket.handle == INVALID_SOCKET) {
            int error = get_last_socket_error();
            exit_with_error("Couldn't accept client connection. Error: %d - %s", error, get_error_description(error));
        }

        char client_ip[INET6_ADDRSTRLEN] = { };
        const char *client_ip_result = ppchat_inet_ntop(hints.ai_family, &client_address.sin_addr, client_ip, sizeof(client_ip));
        if (client_ip_result != client_ip) {
            int error = get_last_socket_error();
            exit_with_error("Couldn't convert client network address to text form. Error: %d - %s", error, get_error_description(error));
        }

        log("New connection from client '%s'.", client_ip);

        SocketContext listen_context;
        listen_context.socket = &client_socket;
        memcpy(listen_context.client_ip, client_ip, sizeof(client_ip));

        DWORD listen_thread_id;
        HANDLE listen_thread = CreateThread(
            /* Thread attributes   */ NULL,
            /* Stack size          */ 0,
            /* Calling procedure   */ listen_for_incoming_network_data,
            /* Procedure argument  */ &listen_context,
            /* Creation flags      */ NULL,
            /* Thread ID           */ &listen_thread_id
        );
    }

    return EXIT_SUCCESS;
}

int main(int arguments_count, char *arguments[]) {
    DWORD listen_thread_id;
    HANDLE listen_thread = CreateThread(
        /* Thread attributes   */ NULL,
        /* Stack size          */ 0,
        /* Calling procedure   */ listen_for_incoming_connections,
        /* Procedure argument  */ NULL,
        /* Creation flags      */ NULL,
        /* Thread ID           */ &listen_thread_id
    );

    while (!g_quit) {
        char input_buffer[256] = { };
        char *input_result = fgets(input_buffer, sizeof(input_buffer), stdin);
        if (input_result != input_buffer) {
            if (g_quit)
                return EXIT_SUCCESS;

            g_quit = true;
            log_error("Couldn't read from stdin.");
            return EXIT_FAILURE;
        }

        if (strncmp(input_buffer, "/shutdown", 9) == 0) {
            
            g_quit = true;

        }
    }

    return EXIT_SUCCESS;
}