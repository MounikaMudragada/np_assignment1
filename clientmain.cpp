#include <iostream>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <unistd.h>
#include <calcLib.h> // Includes the calculation library

#define SA struct sockaddr

// Uncomment the following line to enable debug mode
//#define DEBUG

using namespace std;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        cout << "Usage: " << argv[0] << " <host:port>" << endl;
        return -1;
    }

    // Parse host and port from input
    char *host_port_str = strdup(argv[1]);
    char *separator = strrchr(host_port_str, ':');
    if (!separator) {
        cout << "Error: Please use the format <host:port>." << endl;
        free(host_port_str);
        return -1;
    }

    *separator = '\0';
    char *server_hostname = host_port_str;
    int server_port = atoi(separator + 1);

    cout << "Connecting to Host: " << server_hostname << ", Port: " << server_port << "." << endl;

    // Set up server address hints
    struct addrinfo hints{}, *server_address_info;
    hints.ai_family = AF_UNSPEC;     // Support both IPv4 and IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP connection

    // Resolve the server address
    int addr_status = getaddrinfo(server_hostname, separator + 1, &hints, &server_address_info);
    if (addr_status != 0) {
        cerr << "Address resolution error: " << gai_strerror(addr_status) << endl;
        free(host_port_str);
        return -1;
    }

    // Create a socket
    int socket_descriptor = socket(server_address_info->ai_family, server_address_info->ai_socktype, server_address_info->ai_protocol);
    if (socket_descriptor < 0) {
        #ifdef DEBUG
        cerr << "Socket creation failed: " << strerror(errno) << endl;
        #endif
        freeaddrinfo(server_address_info);
        free(host_port_str);
        return -1;
    }

    // Connect to the server
    if (connect(socket_descriptor, server_address_info->ai_addr, server_address_info->ai_addrlen) < 0) {
        #ifdef DEBUG
        cerr << "Connection failed: " << strerror(errno) << endl;
        #endif
        freeaddrinfo(server_address_info);
        close(socket_descriptor);
        free(host_port_str);
        return -1;
    }

    // Clean up
    freeaddrinfo(server_address_info);
    free(host_port_str);

    char server_response_buffer[2000]; // Buffer for server responses
    memset(server_response_buffer, 0, sizeof(server_response_buffer));

    // Receive initial response from the server
    int received_bytes = recv(socket_descriptor, server_response_buffer, sizeof(server_response_buffer) - 1, 0);
    if (received_bytes < 0) {
        #ifdef DEBUG
        cerr << "Error receiving initial response: " << strerror(errno) << endl;
        #endif
        close(socket_descriptor);
        return -1;
    }

    if (received_bytes > 100) {
        cout << "Error: Received unexpected or excessive data. Closing connection." << endl;
        close(socket_descriptor);
        return 0;
    }

    // Check for expected protocol
    if (strstr(server_response_buffer, "TEXT TCP 1.0")) {
        char client_ok_message[] = "OK\n";
        if (send(socket_descriptor, client_ok_message, strlen(client_ok_message), 0) < 0) {
            #ifdef DEBUG
            cerr << "Error sending protocol confirmation: " << strerror(errno) << endl;
            #endif
            close(socket_descriptor);
            return -1;
        }

        // Receive assignment from the server
        memset(server_response_buffer, 0, sizeof(server_response_buffer));
        received_bytes = recv(socket_descriptor, server_response_buffer, sizeof(server_response_buffer) - 1, 0);
        if (received_bytes < 0) {
            #ifdef DEBUG
            cerr << "Error receiving assignment: " << strerror(errno) << endl;
            #endif
            close(socket_descriptor);
            return -1;
        }

        int operand1, operand2, integer_result = 0;
        double double_operand1, double_operand2, double_result = 0.0;
        char operation_type[10];

        // Parse the received assignment
        sscanf(server_response_buffer, "%s %lf %lf", operation_type, &double_operand1, &double_operand2);

        // Perform the operation
        if (strcmp(operation_type, "fadd") == 0) {
            double_result = double_operand1 + double_operand2;
        } else if (strcmp(operation_type, "fsub") == 0) {
            double_result = double_operand1 - double_operand2;
        } else if (strcmp(operation_type, "fmul") == 0) {
            double_result = double_operand1 * double_operand2;
        } else if (strcmp(operation_type, "fdiv") == 0) {
            double_result = double_operand1 / double_operand2;
        } else {
            operand1 = (int)double_operand1;
            operand2 = (int)double_operand2;

            if (strcmp(operation_type, "add") == 0) {
                integer_result = operand1 + operand2;
            } else if (strcmp(operation_type, "sub") == 0) {
                integer_result = operand1 - operand2;
            } else if (strcmp(operation_type, "mul") == 0) {
                integer_result = operand1 * operand2;
            } else if (strcmp(operation_type, "div") == 0) {
                integer_result = operand1 / operand2;
            }
        }

        char result_message[50];
        if (operation_type[0] == 'f') {
            sprintf(result_message, "%8.8g\n", double_result);
        } else {
            sprintf(result_message, "%d\n", integer_result);
        }

        // Send the result back to the server
        if (send(socket_descriptor, result_message, strlen(result_message), 0) < 0) {
            #ifdef DEBUG
            cerr << "Error sending the result: " << strerror(errno) << endl;
            #endif
            close(socket_descriptor);
            return -1;
        }

        // Receive final server response
        memset(server_response_buffer, 0, sizeof(server_response_buffer));
        received_bytes = recv(socket_descriptor, server_response_buffer, sizeof(server_response_buffer) - 1, 0);
        if (received_bytes < 0) {
            #ifdef DEBUG
            cerr << "Error receiving final server response: " << strerror(errno) << endl;
            #endif
            close(socket_descriptor);
            return -1;
        }

        cout << "Server Response: " << server_response_buffer << endl;
        cout << "Test Completed Successfully." << endl;
    } else {
        cout << "Unexpected protocol or data received. Test failed." << endl;
        close(socket_descriptor);
        return 0;
    }

    // Close the socket before exiting
    close(socket_descriptor);
    return 0;
}
