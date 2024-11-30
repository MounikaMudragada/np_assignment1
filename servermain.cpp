#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <math.h>
#include "calcLib.h"

#define MAX_QUEUE 5              // Maximum client connections in the queue
#define RESPONSE_TIMEOUT 5       // Timeout (seconds) for client responses
#define PROTOCOL_MESSAGE "TEXT TCP 1.0\n\n" // Protocol initialization message
#define BUFFER_SIZE 1024         // Size for buffer
#define FLOAT_PRECISION 0.0001   // Tolerance for floating-point comparison

/**
 * Get the readable IP address from a sockaddr structure (IPv4 or IPv6).
 */
void *extract_ip_address(struct sockaddr *addr) {
    if (addr->sa_family == AF_INET) { // IPv4
        return &(((struct sockaddr_in*)addr)->sin_addr);
    } else { // IPv6
        return &(((struct sockaddr_in6*)addr)->sin6_addr);
    }
}

/**
 * Verify the correctness of a floating-point result sent by the client.
 *
 * @param operation: The operation type (e.g., "fadd", "fsub").
 * @param operand1: First operand (floating-point).
 * @param operand2: Second operand (floating-point).
 * @param client_result: The result received from the client.
 * @return: 1 if the result is correct, 0 otherwise.
 */
int check_float_result(const char* operation, double operand1, double operand2, double client_result) {
    double expected_result = 0.0;

    if (strcmp(operation, "fadd") == 0) {
        expected_result = operand1 + operand2;
    } else if (strcmp(operation, "fsub") == 0) {
        expected_result = operand1 - operand2;
    } else if (strcmp(operation, "fmul") == 0) {
        expected_result = operand1 * operand2;
    } else if (strcmp(operation, "fdiv") == 0) {
        expected_result = operand1 / operand2;
    }

    return fabs(expected_result - client_result) < FLOAT_PRECISION; // Compare within tolerance
}

/**
 * Verify the correctness of an integer result sent by the client.
 *
 * @param operation: The operation type (e.g., "add", "sub").
 * @param operand1: First operand (integer).
 * @param operand2: Second operand (integer).
 * @param client_result: The result received from the client.
 * @return: 1 if the result is correct, 0 otherwise.
 */
int check_integer_result(const char* operation, int operand1, int operand2, int client_result) {
    int expected_result = 0;

    if (strcmp(operation, "add") == 0) {
        expected_result = operand1 + operand2;
    } else if (strcmp(operation, "sub") == 0) {
        expected_result = operand1 - operand2;
    } else if (strcmp(operation, "mul") == 0) {
        expected_result = operand1 * operand2;
    } else if (strcmp(operation, "div") == 0) {
        expected_result = operand1 / operand2;
    }

    return expected_result == client_result;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <IP:PORT>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    initCalcLib(); // Initialize calculation library for random operations

    // Parse server IP and port
    char *server_ip = strtok(argv[1], ":");
    char *server_port = strtok(NULL, ":");
    if (server_ip == NULL || server_port == NULL) {
        fprintf(stderr, "Error: Invalid format. Use IP:PORT.\n");
        exit(EXIT_FAILURE);
    }

    // Server socket setup
    int server_socket;
    struct addrinfo hints, *server_info, *addr;
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len;
    char client_ip_str[INET6_ADDRSTRLEN];
    int opt_reuse = 1;

    fd_set active_fds; // File descriptor set for monitoring client activity
    struct timeval timeout; // Timeout structure

    // Address setup
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // Support both IPv4 and IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP socket
    hints.ai_flags = AI_PASSIVE;     // Automatically bind to the host IP

    // Resolve address and port
    if (getaddrinfo(server_ip, server_port, &hints, &server_info) != 0) {
        perror("Address resolution failed");
        exit(EXIT_FAILURE);
    }

    // Create and bind the server socket
    for (addr = server_info; addr != NULL; addr = addr->ai_next) {
        server_socket = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (server_socket == -1) {
            perror("Socket creation failed");
            continue;
        }

        // Allow reuse of the address
        if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt_reuse, sizeof(int)) == -1) {
            perror("Setting socket options failed");
            close(server_socket);
            exit(EXIT_FAILURE);
        }

        if (bind(server_socket, addr->ai_addr, addr->ai_addrlen) == -1) {
            perror("Socket binding failed");
            close(server_socket);
            continue;
        }

        break; // Successfully bound
    }

    freeaddrinfo(server_info); // Clean up address information

    if (addr == NULL) {
        fprintf(stderr, "Failed to bind to any address\n");
        exit(EXIT_FAILURE);
    }

    // Start listening for incoming connections
    if (listen(server_socket, MAX_QUEUE) == -1) {
        perror("Listening failed");
        exit(EXIT_FAILURE);
    }

    printf("Server running on %s:%s\n", server_ip, server_port);

    while (1) {
        client_addr_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket == -1) {
            perror("Failed to accept connection");
            continue;
        }

        // Convert client IP to readable string
        inet_ntop(client_addr.ss_family, extract_ip_address((struct sockaddr*)&client_addr), client_ip_str, sizeof(client_ip_str));
        printf("Connected to client: %s\n", client_ip_str);

        // Send protocol message
        if (send(client_socket, PROTOCOL_MESSAGE, strlen(PROTOCOL_MESSAGE), 0) == -1) {
            perror("Failed to send protocol message");
            close(client_socket);
            continue;
        }

        // Prepare for client response
        FD_ZERO(&active_fds);
        FD_SET(client_socket, &active_fds);
        timeout.tv_sec = RESPONSE_TIMEOUT;
        timeout.tv_usec = 0;

        // Wait for acknowledgment ("OK\n")
        int activity = select(client_socket + 1, &active_fds, NULL, NULL, &timeout);
        if (activity <= 0) {
            printf("Client response timed out.\n");
            send(client_socket, "ERROR TO\n", 9, 0);
            close(client_socket);
            continue;
        }

        char buffer[BUFFER_SIZE];
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0 || strcmp(buffer, "OK\n") != 0) {
            printf("Invalid client response: %s\n", buffer);
            close(client_socket);
            continue;
        }

        // Generate random operation
        char* operation = randomType();
        double op1_f, op2_f, result_f_client;
        int op1_i, op2_i, result_i_client;

        memset(buffer, 0, sizeof(buffer));
        if (operation[0] == 'f') {
            op1_f = randomFloat();
            op2_f = randomFloat();
            sprintf(buffer, "%s %8.8g %8.8g\n", operation, op1_f, op2_f);
        } else {
            op1_i = randomInt();
            op2_i = randomInt();
            sprintf(buffer, "%s %d %d\n", operation, op1_i, op2_i);
        }

        // Send task to client
        send(client_socket, buffer, strlen(buffer), 0);
        printf("Task sent to client: %s", buffer);

        // Receive and validate client result
        memset(buffer, 0, sizeof(buffer));
        activity = select(client_socket + 1, &active_fds, NULL, NULL, &timeout);
        if (activity <= 0) {
            printf("Timeout waiting for client result.\n");
            send(client_socket, "ERROR TO\n", 9, 0);
            close(client_socket);
            continue;
        }

        bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            printf("Client disconnected unexpectedly.\n");
            close(client_socket);
            continue;
        }

        if (operation[0] == 'f') {
            sscanf(buffer, "%lf", &result_f_client);
            if (check_float_result(operation, op1_f, op2_f, result_f_client)) {
                send(client_socket, "OK\n", 3, 0);
                printf("Correct floating-point result.\n");
            } else {
                send(client_socket, "ERROR\n", 6, 0);
                printf("Incorrect floating-point result.\n");
            }
        } else {
            sscanf(buffer, "%d", &result_i_client);
            if (check_integer_result(operation, op1_i, op2_i, result_i_client)) {
                send(client_socket, "OK\n", 3, 0);
                printf("Correct integer result.\n");
            } else {
                send(client_socket, "ERROR\n", 6, 0);
                printf("Incorrect integer result.\n");
            }
        }

        close(client_socket); // Close client connection
    }

    close(server_socket); // Close server socket
    return 0;
}
