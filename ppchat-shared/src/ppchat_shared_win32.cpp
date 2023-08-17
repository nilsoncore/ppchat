#include "../include/ppchat_shared.h"

#include <stdlib.h>

// Need to link with
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

static char g_error_message[PPCHAT_ERROR_MESSAGE_BUFFER_SIZE] = { };

InputQueue create_input_queue(size_t max_items, size_t item_size) {
	InputQueue queue;

	// MSDN: "This function always succeeds and returns a nonzero value."
	(void) InitializeCriticalSectionAndSpinCount(&queue.critical_section, 500);

	queue.max_items = max_items;
	queue.item_size = item_size;
	queue.items_count = 0;
	queue.front_item_index = 0;
	queue.buffer = (char *) calloc(max_items * item_size, sizeof(char));
	return queue;
}

bool get_next_queue(InputQueue *queue, char **out_queue) {
	if (queue->items_count < 1)
		return false;

	if (out_queue)
		*out_queue = &(queue->buffer[queue->front_item_index * queue->item_size]);

	queue->items_count -= 1;
	queue->front_item_index += 1;
	if (queue->front_item_index >= queue->max_items)
		queue->front_item_index = 0;

	memset(&(queue->buffer[queue->front_item_index * queue->item_size]), 0, queue->item_size);

	return true;
}

bool queue_input(InputQueue *queue, char *input) {
	if (queue->items_count >= queue->max_items)
		return false;

	memcpy(
		&(queue->buffer[queue->front_item_index * queue->item_size]),
		input,
		min(queue->item_size, strlen(input) * sizeof(*input))
	);

	queue->items_count += 1;
	return true;
}

void destroy_input_queue(InputQueue *queue) {
	DeleteCriticalSection(&queue->critical_section);
	free(queue->buffer);
	memset(queue, 0, sizeof(*queue));
}

tm get_current_local_time() {
	time_t current_time = time(NULL);
	tm local_time = { };
	errno_t local_time_result = localtime_s(&local_time, &current_time);
	return local_time;
}

void log_message(FILE *stream, const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);

	tm local_time = get_current_local_time();

	char message[2048];
	size_t message_size = sizeof(message);
	int written = vsnprintf(message, message_size, format, arguments);

	if (written > message_size) {
		fprintf(stderr, "WARNING: Console print exceeds %lld characters limit! The rest is trimmed. (format was: \"%s\")\n", message_size, format);
	}

	fputs(message, stream);
	va_end(arguments);
}

int get_last_socket_error() {
	return WSAGetLastError();
}

int clamp(int min_value, int max_value, int value) {
	if (value < min_value) return min_value;
	if (value > max_value) return max_value;
	return value;
}

char *get_error_description(int error, char *out_message, size_t message_length) {
	DWORD written_without_null_char = FormatMessageA(
		/* Flags               */ FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
		/* Source              */ NULL,
		/* Error code          */ error,
		/* Message language    */ MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		/* Message buffer      */ out_message,
		/* Message buffer size */ message_length,
		/* Arguments           */ NULL
	);

	return out_message;
}

void exit_process_with_error() {
	ExitProcess(EXIT_FAILURE);
}

Socket ppchat_create_socket(int address_family, int socket_type, int protocol) {
	Socket result_socket;
	result_socket.handle = socket(address_family, socket_type, protocol);
	return result_socket;
}

int ppchat_bind(Socket socket, const sockaddr *address_name, int address_name_length) {
	return bind(socket.handle, address_name, address_name_length);
}

int ppchat_listen(Socket socket, int max_connections) {
	return listen(socket.handle, max_connections);
}

Socket ppchat_accept(Socket socket, sockaddr *address, int *address_length) {
	Socket result_socket;
	result_socket.handle = accept(socket.handle, address, address_length);
	return result_socket;
}

Socket ppchat_connect(const char *server_ip, const char *server_port, int *out_error) {
	Socket socket;
	socket.handle = INVALID_SOCKET;

	addrinfo hints = { };
	// ai - address info.
	hints.ai_family = AF_INET; // AF_INET - IPv4; AF_INET6 - IPv6.
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	addrinfo *available_server_addresses = NULL;
	int server_address_info_result = getaddrinfo(
		/* Node name (IP)      */ server_ip,
		/* Service name (port) */ server_port,
		/* Address info hints  */ &hints, // Includes: address family, socket type, protocol.
		/* Result array        */ &available_server_addresses // Iteration through array of results is done by result->ai_next.
	);
	if (server_address_info_result != 0) {
		// printf("Couldn't get server address info. Error: %d\n", server_address_info_result);
		if (out_error)
			*out_error = server_address_info_result;

		return socket;
	}

	addrinfo *server = NULL;
	int error = 0;
	for (server = available_server_addresses; server != NULL; server = server->ai_next) {
		socket.handle = WSASocketW(
			/* Address family */ server->ai_family,
			/* Socket type    */ server->ai_socktype,
			/* Protocol       */ server->ai_protocol,
			/* Protocol info  */ NULL,
			/* Socket group   */ NULL,
			/* Flags          */ WSA_FLAG_OVERLAPPED
		);
		if (socket.handle == INVALID_SOCKET) {
			error = get_last_socket_error();
			log_error("Couldn't create socket. Error: %d - %s", error, get_error_description(error, g_error_message, sizeof(g_error_message)));
			if (out_error)
				*out_error = error;

			return socket;
		}

		int connection_result = connect(socket.handle, server->ai_addr, (int) server->ai_addrlen);
		if (connection_result == SOCKET_ERROR) {
			error = get_last_socket_error();
			ppchat_close_socket(&socket);
			continue;
		}

		break;
	}

	freeaddrinfo(available_server_addresses);

	if (out_error)
		*out_error = error;

	return socket;
}

bool ppchat_disconnect(Socket *socket, int disconnect_method, int *out_error) {
	int shutdown_result = shutdown(socket->handle, SD_SEND);
	
	if (out_error)
		*out_error = shutdown_result;

	ppchat_close_socket(socket);
	return shutdown_result != SOCKET_ERROR;
}

int ppchat_receive(Socket socket, char *receive_buffer, int receive_buffer_size, int flags) {
	return recv(socket.handle, receive_buffer, receive_buffer_size, flags);
}

int ppchat_close_socket(Socket *socket) {
	socket->handle = INVALID_SOCKET;
	return closesocket(socket->handle);
}

int ppchat_send(Socket socket, char *send_buffer, int send_buffer_size, int flags) {
	return send(socket.handle, send_buffer, send_buffer_size, flags);
}

int ppchat_getaddrinfo(const char *ip, const char *port, addrinfo *hints, addrinfo **out_addresses) {
	return getaddrinfo(ip, port, hints, out_addresses);
}

void ppchat_freeaddrinfo(addrinfo *address_info) {
	freeaddrinfo(address_info);
}

const char *ppchat_inet_ntop(int address_family, const void *address, char *out_buffer, size_t out_buffer_size) {
	return inet_ntop(address_family, address, out_buffer, out_buffer_size);
}

BOOL WINAPI DllMain(HINSTANCE dll_instance, DWORD calling_reason, LPVOID reserved) {
	switch (calling_reason) {
		case DLL_PROCESS_ATTACH: {

			log_debug("DLL_PROCESS_ATTACH");

			WSADATA windows_sockets_data;
			int startup_result = WSAStartup(
				/* Windows Sockets version            */ MAKEWORD(2, 2),
				/* Windows Sockets API Data structure */ &windows_sockets_data
			);
			if (startup_result != 0) {
				int error = WSAGetLastError();
				log_error("Couldn't initialize sockets. Error: %d - %s", error, get_error_description(error));
				return FALSE;
			}

			break;
		};
		case DLL_PROCESS_DETACH: {

			log_debug("DLL_PROCESS_DETACH");

			WSACleanup();

			break;
		}
		case DLL_THREAD_ATTACH: {

			log_debug("DLL_THREAD_ATTACH");

			break;
		};
		case DLL_THREAD_DETACH: {
			
			log_debug("DLL_THREAD_DETACH");

			break;
		};
	}

	return TRUE;
}