#include "../../ppchat-shared/include/ppchat_shared.h"

#include <stdlib.h>

char g_error_message[PPCHAT_ERROR_MESSAGE_BUFFER_SIZE] = { };

bool g_quit = false;
bool g_echo_back = false;
time_t g_start_time;

DWORD CALLBACK listen_for_incoming_network_data(void *context) {
	SocketContext *ctx = static_cast<SocketContext *>(context);

	// Store the socket handle value locally for now.
	// Socket socket = *ctx->socket;

	if (ctx->socket->handle == INVALID_SOCKET) {
		log_error("Couldn't listen for incoming network data because connection socket was invalid.");
		return EXIT_FAILURE;
	}

	char receive_buffer[PPCHAT_RECEIVE_BUFFER_SIZE + 1];
	size_t receive_buffer_size = sizeof(receive_buffer);
	int bytes_received = 0;
	do {
		memset(receive_buffer, 0, receive_buffer_size);
		bytes_received = ppchat_receive(*ctx->socket, receive_buffer, receive_buffer_size - 1, NULL);
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
					log_error("Couldn't receive network data. Error: %d - %s", error, get_error_description(error, g_error_message, sizeof(g_error_message)));
				};
			}

			ppchat_close_socket(ctx->socket);
			
		} else if (bytes_received == 0) {

			/* Connection was gratefully closed. */

			log("Connection with '%s' has been closed.", ctx->client_ip);

			int disconnect_error;
			bool disconnected = ppchat_disconnect(ctx->socket, SD_SEND, &disconnect_error);
			if (!disconnected) {
				log_error("Couldn't disconnect from '%s'. Error: %d - %s", ctx->client_ip, disconnect_error, get_error_description(disconnect_error, g_error_message, sizeof(g_error_message)));
			}

		} else {

			/* Network data received. */

			// Null-terminate string.
			int last_character_index = clamp(0, receive_buffer_size-1, bytes_received);
			receive_buffer[last_character_index] = '\0';

			log("Received %d bytes from '%s'. Message: \"%s\"", bytes_received, ctx->client_ip, receive_buffer);

			if (g_echo_back) {
				int bytes_sent = ppchat_send(*ctx->socket, receive_buffer, bytes_received, 0);
				if (bytes_sent == SOCKET_ERROR) {
					int error = get_last_socket_error();
					log_error("Couldn't send message to '%s'. Error: %d - %s", ctx->client_ip, error, get_error_description(error, g_error_message, sizeof(g_error_message)));
					ppchat_close_socket(ctx->socket);
				}

				log("Sent %d bytes to '%s'. Message: \"%s\"", bytes_sent, ctx->client_ip, receive_buffer);
			}

		}
	} while (bytes_received > 0 && !g_quit);
}

DWORD CALLBACK listen_for_incoming_connections(void *context) {
	(void)context;

	addrinfo hints = { };
	// ai - address info.

	// ai - address info.
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	// MSDN: "Setting the AI_PASSIVE flag indicates the caller intends to
	// use the returned socket address structure in a call to the bind function."
	hints.ai_flags = AI_PASSIVE;

	addrinfo *server;
	int server_address_info_result = ppchat_getaddrinfo(
		/* Node name (IP)      */ NULL,
		/* Service name (port) */ PPCHAT_DEFAULT_PORT,
		/* Address info hints  */ &hints,
		/* Result array        */ &server  // Iteration through array of results is done by result->ai_next.
	);
	if (server_address_info_result != 0) {
		exit_with_error("Couldn't get server address info. Error: %d - %s", server_address_info_result, get_error_description(server_address_info_result, g_error_message, sizeof(g_error_message)));
	}

	Socket listen_socket = ppchat_create_socket(server->ai_family, server->ai_socktype, server->ai_protocol);
	if (listen_socket.handle == INVALID_SOCKET) {
		int error = get_last_socket_error();
		exit_with_error("Couldn't create listen socket. Error: %d - %s", error, get_error_description(error, g_error_message, sizeof(g_error_message)));
	}

	// MSDN: "IPV6_V6ONLY - When this value is zero, a socket created for the AF_INET6 address family
	// can be used to send and receive packets to and from an IPv6 address or an IPv4 address.
	// Note that the ability to interact with an IPv4 address requires the use of IPv4 mapped addresses."
	DWORD ipv6_only = 0;
	int ipv6_only_set_result = ppchat_set_socket_option(listen_socket, IPPROTO_IPV6, IPV6_V6ONLY, (const char *) &ipv6_only, sizeof(ipv6_only));
	if (ipv6_only_set_result == SOCKET_ERROR) {
		int error = get_last_socket_error();
		log_error("Couldn't turn off IPV6_V6ONLY. This means that no connection to IPv4 address can be made. Error: %d - %s", error, get_error_description(error, g_error_message, sizeof(g_error_message)));
	}

	int bind_result = ppchat_bind(listen_socket, server->ai_addr, (int) server->ai_addrlen);
	if (bind_result == SOCKET_ERROR) {
		int error = get_last_socket_error();
		exit_with_error("Couldn't bind listen socket. Error: %d - %s", error, get_error_description(error, g_error_message, sizeof(g_error_message)));
	}

	ppchat_freeaddrinfo(server);

	int listen_result = ppchat_listen(listen_socket, SOMAXCONN);
	if (listen_result == SOCKET_ERROR) {
		int error = get_last_socket_error();
		exit_with_error("Couldn't listen on listen socket. Error: %d - %s", error, get_error_description(error, g_error_message, sizeof(g_error_message)));
	}

	while (!g_quit) {
		sockaddr_in6 client_address = { };
		int client_address_size = sizeof(client_address);
		Socket client_socket = ppchat_accept(listen_socket, (sockaddr *) &client_address, &client_address_size);
		if (client_socket.handle == INVALID_SOCKET) {
			int error = get_last_socket_error();
			exit_with_error("Couldn't accept client connection. Error: %d - %s", error, get_error_description(error, g_error_message, sizeof(g_error_message)));
		}

		char client_ip[INET6_ADDRSTRLEN] = { };
		const char *client_ip_result = ppchat_inet_ntop(AF_INET6, &client_address.sin6_addr, client_ip, sizeof(client_ip));
		if (client_ip_result != client_ip) {
			int error = get_last_socket_error();
			exit_with_error("Couldn't convert client network address to text form. Error: %d - %s", error, get_error_description(error, g_error_message, sizeof(g_error_message)));
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

	g_start_time = time(NULL);

	// Print startup message.
	{
		char time[64] = { };
		size_t written = 0;
		tm *internal_time_structure = localtime(&g_start_time);
		tm time_structure = *internal_time_structure;
		log("Server have been started at %s.", ppchat_get_date_and_time(time, sizeof(time), &time_structure, &written));
	}

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

		if (input_buffer[0] == '/') {

			if (strcmp(input_buffer, "/shutdown") == 0 ||
			    strcmp(input_buffer, "/quit") == 0) {

				g_quit = true;

				log("Shutting down the server...");

			} else if (strcmp(input_buffer, "/echo_back") == 0) {

				g_echo_back = !g_echo_back;
				log("Echo back has been %s.", (g_echo_back) ? "enabled" : "disabled");

			}

		}
	}

	return EXIT_SUCCESS;
}
