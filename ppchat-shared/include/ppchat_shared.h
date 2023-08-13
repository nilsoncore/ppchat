#ifndef PPCHAT_SHARED_H
#define PPCHAT_SHARED_H

#define PPCHAT_API __declspec(dllexport)

#include <stdio.h>
#include <stdint.h>
#include <time.h>

#define WIN32_LEAN_AND_MEAN
#include <ws2tcpip.h>
#include <windows.h>

// Terminal color escape codes:
// 
// Escape enclosings:
//     "\x1b[" - starts escape code;
//     "m" - ends escape code.
// 
// Parameters inside escape enclosings:
//     "38;2;R;G;B" - sets foreground color to RGB;
//     "0" - resets all effects.
#define CONSOLE_COLOR_RED    "\x1b[38;2;255;100;100m"
#define CONSOLE_COLOR_YELLOW "\x1b[38;2;255;255;130m"
#define CONSOLE_COLOR_GRAY   "\x1b[38;2;130;130;130m"
#define CONSOLE_COLOR_RESET  "\x1b[0m"

// This is a hacky macro that prints log message into standard out with timestamp as prefix.
// It could be done in normal function but it would require two passes to
// format the log message and then format that log message with timestamp.
#define log(format, ...) {                       \
    tm __my_log_time = get_current_local_time(); \
    log_message(                                 \
        stdout,                                  \
        "[%02d:%02d:%02d] " format "\n",         \
        __my_log_time.tm_hour,                   \
        __my_log_time.tm_min,                    \
        __my_log_time.tm_sec,                    \
        __VA_ARGS__                              \
    );                                           \
}

// Same as `log(format, ...)` macro, but prints into standard error.
#define log_warning(format, ...) {               \
    tm __my_log_time = get_current_local_time(); \
    log_message(                                 \
        stderr,                                  \
        CONSOLE_COLOR_YELLOW "[%02d:%02d:%02d] WARNING: " format CONSOLE_COLOR_RESET "\n", \
        __my_log_time.tm_hour,                   \
        __my_log_time.tm_min,                    \
        __my_log_time.tm_sec,                    \
        __VA_ARGS__                              \
    );                                           \
}

// Same as `log(format, ...)` macro, but prints into standard error.
#define log_error(format, ...) {                 \
	tm __my_log_time = get_current_local_time(); \
	log_message(                                 \
        stderr,                                  \
        CONSOLE_COLOR_RED "[%02d:%02d:%02d] ERROR: " format CONSOLE_COLOR_RESET "\n", \
        __my_log_time.tm_hour,                   \
        __my_log_time.tm_min,                    \
        __my_log_time.tm_sec,                    \
        __VA_ARGS__                              \
	);                                           \
}

#ifdef _DEBUG
#define log_debug(format, ...) {                 \
    tm __my_log_time = get_current_local_time(); \
    log_message(                                 \
        stdout,                                  \
        CONSOLE_COLOR_GRAY "[%02d:%02d:%02d] DEBUG: " format CONSOLE_COLOR_RESET "\n", \
        __my_log_time.tm_hour,                   \
        __my_log_time.tm_min,                    \
        __my_log_time.tm_sec,                    \
        __VA_ARGS__                              \
    );                                           \
}
#else /* _DEBUG */
#define log_debug(format, ...)
#endif /* _DEBUG */

// Prints a provided error message through macro `log_error()`
// and exits process with error code `EXIT_FAILURE`.
#define exit_with_error(format, ...) { \
    log_error(format, __VA_ARGS__);    \
    exit_process_with_error();         \
}                                      \

const char *PPCHAT_DEFAULT_PORT = "1337";
const int PPCHAT_RECEIVE_BUFFER_SIZE = 4096;
const int PPCHAT_INPUT_QUEUE_ITEM_SIZE = 256;
const int PPCHAT_INPUT_QUEUE_MAX_ITEMS = 4;

typedef struct InputQueue {
	CRITICAL_SECTION critical_section;
	size_t           max_items;
	size_t           item_size;
	unsigned int     items_count;
	unsigned int     front_item_index;
	char            *buffer;
} InputQueue;

typedef struct Socket {
	union {
		uint64_t handle;
		int      _handle_unix;
		SOCKET   _handle_win32;
	};
} Socket;

typedef struct SocketContext {
	Socket socket;
	char client_ip[INET6_ADDRSTRLEN];
} SocketContext;

extern "C" {

PPCHAT_API InputQueue create_input_queue(size_t max_items, size_t item_size);
PPCHAT_API bool get_next_queue(InputQueue *queue, char **out_queue);
PPCHAT_API bool queue_input(InputQueue *queue, char *input);
PPCHAT_API void destroy_input_queue(InputQueue *queue);

PPCHAT_API tm get_current_local_time();
PPCHAT_API void log_message(FILE *stream, const char *format, ...);

PPCHAT_API int get_last_socket_error();
PPCHAT_API char *get_error_description(int error);

PPCHAT_API void exit_process_with_error();

PPCHAT_API int clamp(int min_value, int max_value, int value);

PPCHAT_API Socket ppchat_create_socket(int address_family, int socket_type, int protocol);
PPCHAT_API int ppchat_bind(Socket socket, const sockaddr *address_name, int address_name_length);
PPCHAT_API int ppchat_listen(Socket socket, int max_connections);
PPCHAT_API Socket ppchat_accept(Socket socket, sockaddr *address, int *address_length);
PPCHAT_API Socket ppchat_connect(const char *ip, const char *port, int *out_error);
PPCHAT_API bool ppchat_disconnect(Socket connection_socket, int disconnect_method, int *out_error);
PPCHAT_API int ppchat_receive(Socket socket, char *receive_buffer, int receive_buffer_size, int flags);
PPCHAT_API int ppchat_close_socket(Socket socket);
PPCHAT_API int ppchat_send(Socket socket, char *send_buffer, int send_buffer_size, int flags);
PPCHAT_API int ppchat_getaddrinfo(const char *ip, const char *port, addrinfo *hints, addrinfo **out_addresses);
PPCHAT_API void ppchat_freeaddrinfo(addrinfo *address_info);
PPCHAT_API const char *ppchat_inet_ntop(int address_family, const void *address, char *out_buffer, size_t out_buffer_size);

}

#endif /* PPCHAT_SHADER_H */