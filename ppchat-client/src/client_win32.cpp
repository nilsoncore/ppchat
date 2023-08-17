#include "../../ppchat-shared/include/ppchat_shared.h"

#include <stdlib.h>

const char *DEFAULT_FILE_SAVE_FOLDER = "D:/Downloads/";

InputQueue g_input_queue;
Socket g_client_socket = { INVALID_SOCKET };

char g_error_message[PPCHAT_ERROR_MESSAGE_BUFFER_SIZE] = { };

char g_connected_server_ip[INET6_ADDRSTRLEN] = { };
char g_connected_server_port[6] = { };

bool g_quit = false;

DWORD WINAPI handle_incoming_console_input(void *data) {
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

		EnterCriticalSection(&g_input_queue.critical_section);
		queue_input(&g_input_queue, input_buffer);
		LeaveCriticalSection(&g_input_queue.critical_section);
	}

	return EXIT_SUCCESS;
}

bool get_next_console_input(char **out_input) {
	EnterCriticalSection(&g_input_queue.critical_section);
	bool got_input = get_next_queue(&g_input_queue, out_input);
	LeaveCriticalSection(&g_input_queue.critical_section);

	return got_input;
}

DWORD CALLBACK listen_for_incoming_network_data(void *context) {
	SocketContext *ctx = static_cast<SocketContext *>(context);

	if (ctx->socket->handle == INVALID_SOCKET)
		return EXIT_FAILURE;

	int bytes_received;
	do {
		char receive_buffer[PPCHAT_RECEIVE_BUFFER_SIZE];
		size_t receive_buffer_size = sizeof(receive_buffer);
		Socket socket = *ctx->socket;
		bytes_received = ppchat_receive(*ctx->socket, receive_buffer, (int) (receive_buffer_size - 1), NULL);
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
				case WSAEINTR: {
					// The socket has been shut down and any further operations
					// are cancelled.
					continue;
				}
				default: {
					log_error("Couldn't receive network data. Error: %d - %s", error, get_error_description(error, g_error_message, sizeof(g_error_message)));
				};
			}

			// ppchat_close_socket(ctx->socket);
			int disconnect_error;
			bool disconnected = ppchat_disconnect(&socket, SD_SEND, &disconnect_error);
			if (!disconnected) {
				log_error("Couldn't disconnect from '%s'. Error: %d - %s", ctx->client_ip, disconnect_error, get_error_description(disconnect_error, g_error_message, sizeof(g_error_message)));
			}

		} else if (bytes_received == 0) {
		
			/* Connection was gratefully closed. */
		
			log("Connection with '%s' has been closed.", ctx->client_ip);

			// ppchat_close_socket(ctx->socket);
			int disconnect_error;
			bool disconnected = ppchat_disconnect(&socket, SD_SEND, &disconnect_error);
			if (!disconnected) {
				log_error("Couldn't disconnect from '%s'. Error: %d - %s", ctx->client_ip, disconnect_error, get_error_description(disconnect_error, g_error_message, sizeof(g_error_message)));
			}

		} else {
		
			/* Network data received. */

			// Null-terminate string.
			int last_character_index = clamp(0, (int) (receive_buffer_size - 1), bytes_received);
			receive_buffer[last_character_index] = '\0';

			log("Received %d bytes from '%s'. Message: \"%s\"", bytes_received, ctx->client_ip, receive_buffer);

		}
	} while (bytes_received > 0 && !g_quit);

	free(ctx);

	return EXIT_SUCCESS;
}

void poll_console_input() {
	char *input = NULL;
	while (!g_quit && get_next_console_input(&input)) {

		size_t input_length = strlen(input);
		if (input_length < 2)
			// We need at least 2 characters because 1 would
			// always be a new line character at the end.
			continue;

		// Remove new line character at the end of the input string.
		char *last_character = &input[input_length - 1];
		if (isspace(*last_character))
			*last_character = '\0';

		if (input[0] == '/') {

			size_t first_space_position = strcspn(input, " ");
			char *next_input_token = NULL;
			char *command = strtok_s(input, " ", &next_input_token);

			if (strcmp(command, "/connect") == 0) {

				if (g_client_socket.handle != INVALID_SOCKET) {
					log("You are already connected to server '%s:%s'.", g_connected_server_ip, g_connected_server_port);
					continue;
				}

				char *input_argument = strtok_s(NULL, " ", &next_input_token);
				if (!input_argument) {
					log("You didn't provide any arguments. Use: \"connect <ip> [port]\", or \"connect <ip:port>\".");
					continue;
				}

				size_t first_colon_position = strcspn(input_argument, ":");

				const char *server_ip;
				const char *server_port;
				bool port_colon_found = (first_colon_position != strlen(input_argument));
				if (port_colon_found) {
					char *next_argument_token = NULL;
					server_ip = strtok_s(input_argument, ":", &next_argument_token);
					server_port = strtok_s(NULL, ":", &next_argument_token);
				} else {
					server_ip = input_argument;
					input_argument = strtok_s(NULL, " ", &next_input_token);
					server_port = (input_argument) ? input_argument : PPCHAT_DEFAULT_PORT;
				}

				int connection_error;
				g_client_socket = ppchat_connect(server_ip, server_port, &connection_error);
				if (g_client_socket.handle != INVALID_SOCKET) {
					log("Connected to server '%s:%s'.", server_ip, server_port);
					memcpy(g_connected_server_ip, server_ip, strlen(server_ip));
					memcpy(g_connected_server_port, server_port, strlen(server_port));

					SocketContext *listen_context = (SocketContext *) calloc(1, sizeof(*listen_context));
					listen_context->socket = &g_client_socket;
					memcpy(listen_context->client_ip, server_ip, strlen(server_ip));

					DWORD input_thread_id;
					HANDLE input_thread = CreateThread(
						/* Thread attributes   */ NULL,
						/* Stack size          */ 0,
						/* Calling procedure   */ listen_for_incoming_network_data,
						/* Procedure argument  */ listen_context,
						/* Creation flags      */ NULL,
						/* Thread ID           */ &input_thread_id
					);
				} else {
					if (connection_error == 0) {
						log("Couldn't connect to server '%s:%s'.", server_ip, server_port);
					} else {
						log_error("Couldn't connect to server '%s:%s'. Error: %d - %s", server_ip, server_port, connection_error, get_error_description(connection_error, g_error_message, sizeof(g_error_message)));
					}
				}

			} else if (strcmp(command, "/send") == 0) {

				if (g_client_socket.handle == INVALID_SOCKET) {
					log("You are not connected to any server.");
					continue;
				}

				if (input_length < 7) {
					log("Message has to be at least 1 character long.");
					continue;
				}

				char *message = &input[6];
				int bytes_sent = ppchat_send(g_client_socket, message, (int) strlen(message), NULL);
				if (bytes_sent == SOCKET_ERROR) {
					int error = get_last_socket_error();
					exit_with_error("Couldn't send message to '%s:%s'. Error: %d - %s", g_connected_server_ip, g_connected_server_port, error, get_error_description(error, g_error_message, sizeof(g_error_message)));
				} else {
					log("Sent message: \"%s\" (%d bytes).", message, bytes_sent);
				}


			} else if (strcmp(command, "/send_file") == 0) {



			} else if (strcmp(command, "/disconnect") == 0) {

				if (g_client_socket.handle == INVALID_SOCKET) {
					log("You are not connected to any server.");
					continue;
				}

				int disconnect_error = 0;
				bool disconnected = ppchat_disconnect(&g_client_socket, SD_SEND, &disconnect_error);
				if (!disconnected) {
					int error = get_last_socket_error();
					exit_with_error("Couldn't shutdown client socket connection with '%s:%s'. Error: %d - %s", g_connected_server_ip, g_connected_server_port, error, get_error_description(error, g_error_message, sizeof(g_error_message)));
				}

				log("Disconnected from '%s:%s'.", g_connected_server_ip, g_connected_server_port);
				memset(g_connected_server_ip, 0, sizeof(g_connected_server_ip));
				memset(g_connected_server_port, 0, sizeof(g_connected_server_port));

			} else if (strcmp(command, "/quit") == 0) {

				g_quit = true;

			} 
		
		} else {

			/* Treat non-command input as an argument to implicit /send command. */
			
			if (g_client_socket.handle == INVALID_SOCKET) {
				log("You are not connected to any server.");
				continue;
			}

			int bytes_sent = ppchat_send(g_client_socket, input, (int) input_length, NULL);
			if (bytes_sent == SOCKET_ERROR) {
				int error = get_last_socket_error();
				exit_with_error("Couldn't send message to '%s:%s'. Error: %d - %s", g_connected_server_ip, g_connected_server_port, error, get_error_description(error, g_error_message, sizeof(g_error_message)));
			} else {
				log("Sent message: \"%s\" (%d bytes).", input, bytes_sent);
			}

		}
	}
}

int main(int arguments_count, char *arguments[]) {
	g_input_queue = create_input_queue(PPCHAT_INPUT_QUEUE_MAX_ITEMS, PPCHAT_INPUT_QUEUE_ITEM_SIZE);

	DWORD input_thread_id;
	HANDLE input_thread = CreateThread(
		/* Thread attributes   */ NULL,
		/* Stack size          */ 0,
		/* Calling procedure   */ handle_incoming_console_input,
		/* Procedure argument  */ NULL,
		/* Creation flags      */ NULL,
		/* Thread ID           */ &input_thread_id
	);

	while (!g_quit) {
		poll_console_input();
		Sleep(10);
	}

	if (g_client_socket.handle != INVALID_SOCKET)
		ppchat_close_socket(&g_client_socket);

	destroy_input_queue(&g_input_queue);

	return EXIT_SUCCESS;
}
