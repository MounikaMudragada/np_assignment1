#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <math.h>
#include <calcLib.h>

#define MAX_QUEUE 5                    // Maximum number of clients that can queue
#define PROTOCOL_MESSAGE "TEXT TCP 1.0\n\n"  // Protocol message to be sent to clients
#define ERROR_TIMEOUT "ERROR TO\n"     // Timeout error message
#define OK_RESPONSE "OK\n"             // OK response for successful operations
#define BUFFER_SIZE 1024               // Buffer size for communication
#define TIMEOUT 5                      // Timeout duration in seconds

volatile sig_atomic_t timeout_occurred = 0;  // Flag to detect timeout

// Timeout signal handler
void handle_alarm(int signo) {
    timeout_occurred = 1;  // Set the timeout flag
}

// Helper function to extract IP address of the client
char *get_ip_str(const struct sockaddr *sa, char *s, size_t maxlen) {
    switch (sa->sa_family) {
        case AF_INET:  // IPv4 address
            inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr), s, maxlen);
            break;
        case AF_INET6:  // IPv6 address
            inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr), s, maxlen);
            break;
        default:  // Unknown address family
            strncpy(s, "Unknown AF", maxlen);
            return NULL;
    }
    return s;
}

// Function to handle communication with a single client
void handle_client(int client_socket) {
    char buffer[BUFFER_SIZE];    // Buffer for receiving data
    char operation[256];         // To hold the operation string
    double fv1, fv2, fresult;    // Floating-point operands and result
    int iv1, iv2, iresult;       // Integer operands and result
    int send_status;             // Status of send operations

    // Send protocol message to the client
    if (send(client_socket, PROTOCOL_MESSAGE, strlen(PROTOCOL_MESSAGE), 0) < 0) {
        perror("Failed to send protocol message");
        close(client_socket);
        return;
    }

    // Wait for "OK\n" from the client
    timeout_occurred = 0;  // Reset timeout flag
    alarm(TIMEOUT);        // Set timeout alarm
    memset(buffer, 0, BUFFER_SIZE);  // Clear the buffer
    if (recv(client_socket, buffer, BUFFER_SIZE, 0) <= 0 || timeout_occurred) {
        if (timeout_occurred) {
            printf("Timeout occurred while waiting for client response\n");
            send(client_socket, ERROR_TIMEOUT, strlen(ERROR_TIMEOUT), 0);
        } else {
            perror("Client failed to respond or protocol mismatch");
        }
        close(client_socket);
        return;
    }
    alarm(0);  // Disable timeout alarm after successful receive

    // Check if client responded with "OK\n"
    if (strncmp(buffer, OK_RESPONSE, strlen(OK_RESPONSE)) != 0) {
        printf("Client did not accept the protocol\n");
        close(client_socket);
        return;
    }

    // Generate random operation and operands
    initCalcLib();  // Initialize the calcLib library
    char *op = randomType();  // Get a random operation type
    fv1 = randomFloat();  // Generate random floating-point value 1
    fv2 = randomFloat();  // Generate random floating-point value 2
    iv1 = randomInt();    // Generate random integer value 1
    iv2 = randomInt();    // Generate random integer value 2

    // If the operation involves floating-point numbers
    if (op[0] == 'f') {
        // Perform the appropriate floating-point operation
        if (strcmp(op, "fadd") == 0) fresult = fv1 + fv2;
        else if (strcmp(op, "fsub") == 0) fresult = fv1 - fv2;
        else if (strcmp(op, "fmul") == 0) fresult = fv1 * fv2;
        else if (strcmp(op, "fdiv") == 0) fresult = fv1 / fv2;

        // Format the operation string
        snprintf(operation, sizeof(operation), "%s %8.8g %8.8g\n", op, fv1, fv2);
    } else {  // If the operation involves integers
        // Perform the appropriate integer operation
        if (strcmp(op, "add") == 0) iresult = iv1 + iv2;
        else if (strcmp(op, "sub") == 0) iresult = iv1 - iv2;
        else if (strcmp(op, "mul") == 0) iresult = iv1 * iv2;
        else if (strcmp(op, "div") == 0) iresult = iv1 / iv2;

        // Format the operation string
        snprintf(operation, sizeof(operation), "%s %d %d\n", op, iv1, iv2);
    }

    // Send the operation to the client
    if (send(client_socket, operation, strlen(operation), 0) < 0) {
        perror("Failed to send operation to client");
        close(client_socket);
        return;
    }

    // Receive the result from the client
    memset(buffer, 0, BUFFER_SIZE);  // Clear the buffer
    timeout_occurred = 0;  // Reset timeout flag
    alarm(TIMEOUT);  // Set timeout for receiving response
    if (recv(client_socket, buffer, BUFFER_SIZE, 0) <= 0 || timeout_occurred) {
        if (timeout_occurred) {
            printf("Timeout occurred while waiting for client result\n");
            send(client_socket, ERROR_TIMEOUT, strlen(ERROR_TIMEOUT), 0);
        } else {
            perror("Client failed to respond with result");
        }
        close(client_socket);
        return;
    }
    alarm(0);  // Disable timeout alarm after successful receive

    // Compare results and send feedback
    if (op[0] == 'f') {
        double client_result = atof(buffer);  // Convert client response to double
        if (fabs(client_result - fresult) < 0.0001) {
            send_status = send(client_socket, OK_RESPONSE, strlen(OK_RESPONSE), 0);
        } else {
            send_status = send(client_socket, "ERROR\n", strlen("ERROR\n"), 0);
        }
    } else {
        int client_result = atoi(buffer);  // Convert client response to integer
        if (client_result == iresult) {
            send_status = send(client_socket, OK_RESPONSE, strlen(OK_RESPONSE), 0);
        } else {
            send_status = send(client_socket, "ERROR\n", strlen("ERROR\n"), 0);
        }
    }

    if (send_status < 0) perror("Failed to send response to client");

    close(client_socket);  // Close the client connection
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s IP:Port\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Parse host and port
    char *host = strtok(argv[1], ":");
    char *port = strtok(NULL, ":");
    if (!host || !port) {
        fprintf(stderr, "Invalid argument format. Use IP:Port\n");
        exit(EXIT_FAILURE);
    }

    struct addrinfo hints, *res, *p;
    int master_socket;
    struct sockaddr_storage client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char client_ip[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0) {
        perror("getaddrinfo failed");
        exit(EXIT_FAILURE);
    }

    for (p = res; p != NULL; p = p->ai_next) {
        master_socket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (master_socket < 0) continue;

        if (bind(master_socket, p->ai_addr, p->ai_addrlen) == 0) break;

        close(master_socket);
    }

    if (!p) {
        perror("Failed to bind");
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(res);

    if (listen(master_socket, MAX_QUEUE) < 0) {
        perror("Listen failed");
        close(master_socket);
        exit(EXIT_FAILURE);
    }

    signal(SIGALRM, handle_alarm);  // Set up signal handler for timeouts

    printf("Server is listening on %s:%s\n", host, port);

    while (1) {
        int client_socket = accept(master_socket, (struct sockaddr *)&client_addr, &addr_len);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }

        // Get and print the client's IP address
        get_ip_str((struct sockaddr *)&client_addr, client_ip, sizeof(client_ip));
        printf("Connection from %s\n", client_ip);

        handle_client(client_socket);  // Handle the client's interaction
    }

    close(master_socket);  // Close the master socket
    return 0;
}
