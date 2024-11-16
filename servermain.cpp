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

// Define constants for client queue size, timeout, protocol message, buffer size, and retry attempts
#define CLIENT_QUEUE_LIMIT 5
#define RESPONSE_TIMEOUT_SECONDS 5
#define INITIAL_PROTOCOL_MESSAGE "TEXT TCP 1.0\n\n"
#define BUFFER_MAX_SIZE 1024
#define SOCKET_CREATION_ATTEMPTS 3           // Maximum buffer size for receiving or sending messages
#define SOCKET_CREATION_ATTEMPTS 3          // Number of retry attempts for socket creation and binding

int main(int argc, char *argv[]) {
    // Validate command-line arguments to ensure the hostname and port are provided
    if (argc != 2) {
        fprintf(stderr, "usage: %s hostname:port\n", argv[0]);
        exit(1);
    }

    // Initialize the calculation library, used for generating random operations and operands
    initCalcLib();

    // Parse hostname and port from the input argument
    char *inputHostname = strtok(argv[1], ":");
    char *inputPort = strtok(NULL, ":");

    // Declare variables for server socket, address structures, and socket options
    int serverSocketDescriptor;
    struct addrinfo addressHints, *serverInfo, *addressPointer;
    struct sockaddr_storage clientSocketAddress;
    socklen_t clientAddressSize;
    char clientIPAddress[INET6_ADDRSTRLEN];
    int socketOptionReuse = 1;

    // Variables for socket read set and timeout configurations
    fd_set socketReadSet;
    struct timeval socketTimeout;

    // Set up address hints for the server
    memset(&addressHints, 0, sizeof addressHints);
    addressHints.ai_family = AF_UNSPEC;      // Allow IPv4 or IPv6
    addressHints.ai_socktype = SOCK_STREAM; // Use TCP
    addressHints.ai_flags = AI_PASSIVE;     // Use the server's IP automatically

    // Retrieve address information for the specified hostname and port
    if (getaddrinfo(inputHostname, inputPort, &addressHints, &serverInfo) != 0) {
        perror("getaddrinfo");
        return 1;
    }

    // Initialize retry logic for socket creation and binding
    int creationAttempts = 0;
    int socketCreated = 0;

    // Retry creating and binding a socket up to SOCKET_CREATION_ATTEMPTS
    for (creationAttempts = 0; creationAttempts < SOCKET_CREATION_ATTEMPTS; creationAttempts++) {
        for (addressPointer = serverInfo; addressPointer != NULL; addressPointer = addressPointer->ai_next) {
            // Create a socket
            serverSocketDescriptor = socket(addressPointer->ai_family, addressPointer->ai_socktype, addressPointer->ai_protocol);
            if (serverSocketDescriptor == -1) {
                perror("server: socket");
                continue;
            }

            // Configure the socket to reuse the address
            if (setsockopt(serverSocketDescriptor, SOL_SOCKET, SO_REUSEADDR, &socketOptionReuse, sizeof(int)) == -1) {
                perror("setsockopt");
                close(serverSocketDescriptor);
                continue;
            }

            // Bind the socket to the specified address and port
            if (bind(serverSocketDescriptor, addressPointer->ai_addr, addressPointer->ai_addrlen) == -1) {
                perror("server: bind");
                close(serverSocketDescriptor);
                continue;
            }

            // If the socket is successfully created and bound, set the flag and exit the loop
            socketCreated = 1;
            break;
        }

        // If the socket was created successfully, break out of the retry loop
        if (socketCreated) break;

        fprintf(stderr, "Retrying socket creation attempt %d...\n", creationAttempts + 1);
        sleep(1); // Wait before retrying
    }

    // Free the address information after usage
    freeaddrinfo(serverInfo);

    // If no socket could be created after all attempts, exit with an error
    if (!socketCreated) {
        fprintf(stderr, "server: failed to create and bind socket after %d attempts\n", SOCKET_CREATION_ATTEMPTS);
        exit(1);
    }

    // Start listening for incoming client connections
    if (listen(serverSocketDescriptor, CLIENT_QUEUE_LIMIT) == -1) {
        perror("listen");
        exit(1);
    }

    printf("Server is waiting for connections...\n");

    // Server loop to handle incoming connections
    while (1) {
        clientAddressSize = sizeof clientSocketAddress;

        // Accept a new client connection
        int clientSocketDescriptor = accept(serverSocketDescriptor, (struct sockaddr *)&clientSocketAddress, &clientAddressSize);
        if (clientSocketDescriptor == -1) {
            perror("accept");
            continue;
        }

        // Extract and display the client's IP address
        if (clientSocketAddress.ss_family == AF_INET) {
            inet_ntop(AF_INET, &(((struct sockaddr_in*)&clientSocketAddress)->sin_addr), clientIPAddress, sizeof clientIPAddress);
        } else {
            inet_ntop(AF_INET6, &(((struct sockaddr_in6*)&clientSocketAddress)->sin6_addr), clientIPAddress, sizeof clientIPAddress);
        }

        printf("Connected to client at %s\n", clientIPAddress);

        // Send the initial protocol message to the client
        send(clientSocketDescriptor, INITIAL_PROTOCOL_MESSAGE, strlen(INITIAL_PROTOCOL_MESSAGE), 0);

        // Initialize the read set for select() to monitor the client socket
        FD_ZERO(&socketReadSet);
        FD_SET(clientSocketDescriptor, &socketReadSet);
        socketTimeout.tv_sec = RESPONSE_TIMEOUT_SECONDS;
        socketTimeout.tv_usec = 0;

        // Wait for the client's "OK\n" message with a timeout
        int clientActivity = select(clientSocketDescriptor + 1, &socketReadSet, NULL, NULL, &socketTimeout);
        if (clientActivity <= 0) {
            printf("Client timeout or error.\n");
            send(clientSocketDescriptor, "ERROR TO\n", 9, 0);
            close(clientSocketDescriptor);
            continue;
        }

        // Receive the "OK\n" message from the client
        char messageBuffer[BUFFER_MAX_SIZE];
        memset(messageBuffer, 0, sizeof(messageBuffer));
        int receivedBytes = recv(clientSocketDescriptor, messageBuffer, BUFFER_MAX_SIZE - 1, 0);
        if (receivedBytes < 0 || strcmp(messageBuffer, "OK\n") != 0) {
            printf("Invalid client response: %s\n", messageBuffer);
            close(clientSocketDescriptor);
            continue;
        }

        // Generate a random operation and operands
        char* randomOperation = randomType();
        double floatOperand1, floatOperand2, floatClientResult;
        int intOperand1, intOperand2, intClientResult;
        memset(messageBuffer, 0, sizeof(messageBuffer));

        if (randomOperation[0] == 'f') {
            floatOperand1 = randomFloat();
            floatOperand2 = randomFloat();
            sprintf(messageBuffer, "%s %8.8g %8.8g\n", randomOperation, floatOperand1, floatOperand2);
        } else {
            intOperand1 = randomInt();
            intOperand2 = randomInt();
            sprintf(messageBuffer, "%s %d %d\n", randomOperation, intOperand1, intOperand2);
        }

        // Send the operation and operands to the client
        send(clientSocketDescriptor, messageBuffer, strlen(messageBuffer), 0);
        printf("Sent to client: %s", messageBuffer);

        // Wait for the client's result response with a timeout
        memset(messageBuffer, 0, sizeof(messageBuffer));
        clientActivity = select(clientSocketDescriptor + 1, &socketReadSet, NULL, NULL, &socketTimeout);
        if (clientActivity <= 0) {
            printf("Client timeout or error receiving result.\n");
            send(clientSocketDescriptor, "ERROR TO\n", 9, 0);
            close(clientSocketDescriptor);
            continue;
        }

        // Receive the client's result
        receivedBytes = recv(clientSocketDescriptor, messageBuffer, BUFFER_MAX_SIZE - 1, 0);
        if (receivedBytes <= 0) {
            printf("Client disconnected or error.\n");
            close(clientSocketDescriptor);
            continue;
        }

        // Validate the client's result based on the operation type
        if (randomOperation[0] == 'f') {
            sscanf(messageBuffer, "%lf", &floatClientResult);
            double computedFloatResult;
            if (strcmp(randomOperation, "fadd") == 0) computedFloatResult = floatOperand1 + floatOperand2;
            else if (strcmp(randomOperation, "fsub") == 0) computedFloatResult = floatOperand1 - floatOperand2;
            else if (strcmp(randomOperation, "fmul") == 0) computedFloatResult = floatOperand1 * floatOperand2;
            else if (strcmp(randomOperation, "fdiv") == 0) computedFloatResult = floatOperand1 / floatOperand2;

            if (fabs(computedFloatResult - floatClientResult) < 0.0001) {
                send(clientSocketDescriptor, "OK\n", 3, 0);
                printf("Correct result from client.\n");
            } else {
                send(clientSocketDescriptor, "ERROR\n", 6, 0);
                printf("Incorrect result from client.\n");
            }
        } else {
            sscanf(messageBuffer, "%d", &intClientResult);
            int computedIntResult;
            if (strcmp(randomOperation, "add") == 0) computedIntResult = intOperand1 + intOperand2;
            else if (strcmp(randomOperation, "sub") == 0) computedIntResult = intOperand1 - intOperand2;
            else if (strcmp(randomOperation, "mul") == 0) computedIntResult = intOperand1 * intOperand2;
            else if (strcmp(randomOperation, "div") == 0) computedIntResult = intOperand1 / intOperand2;

            if (computedIntResult == intClientResult) {
                send(clientSocketDescriptor, "OK\n", 3, 0);
                printf("Correct result from client.\n");
            } else {
                send(clientSocketDescriptor, "ERROR\n", 6, 0);
                printf("Incorrect result from client.\n");
            }
        }

        // Close the client connection after processing
        close(clientSocketDescriptor);
    }

    // Close the server socket when exiting
    close(serverSocketDescriptor);
    return 0;
}
