#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <map>
#include <string>
#include <ctime>
#include <sys/select.h>
#include <calcLib.h>
#include "protocol.h"

using namespace std;

#define MAXBUFLEN 100
#define PROTOCOL_TYPE 22
#define PROTOCOL_MESSAGE 0
#define PROTOCOL_VERSION_MAJOR 1
#define PROTOCOL_VERSION_MINOR 0
#define TIMEOUT_SEC 10

struct ClientData {
    int id;
    string ipAddress;
    int portNumber;
    time_t lastActivity;
    calcProtocol assignment;

    ClientData() : id(0), ipAddress(""), portNumber(0), lastActivity(0) {}

    ClientData(int clientID, const string &ip, int port, const calcProtocol &task)
        : id(clientID), ipAddress(ip), portNumber(port), lastActivity(time(nullptr)), assignment(task) {}
};

map<int, ClientData> activeClients; // Track active clients
int nextClientID = 1;

// Define response messages
const calcMessage RESPONSE_NOT_OK = {htons(2), htonl(2), htonl(17), htons(PROTOCOL_VERSION_MAJOR), htons(PROTOCOL_VERSION_MINOR)};
const calcMessage RESPONSE_OK = {htons(2), htonl(1), htonl(17), htons(PROTOCOL_VERSION_MAJOR), htons(PROTOCOL_VERSION_MINOR)};

unordered_map<string, int> opIndex = {
    {"add", 1}, {"sub", 2}, {"mul", 3}, {"div", 4},
    {"fadd", 5}, {"fsub", 6}, {"fmul", 7}, {"fdiv", 8}};

int getArithIndex(const string &operation) {
    return opIndex[operation];
}

void sendResponse(int socketFD, const sockaddr_storage &clientAddr, socklen_t addrLen, const calcMessage &response) {
    if (sendto(socketFD, &response, sizeof(response), 0, (struct sockaddr *)&clientAddr, addrLen) == -1) {
        perror("sendto");
    }
}

void cleanupTimedOutClients() {
    time_t currentTime = time(nullptr);
    for (auto it = activeClients.begin(); it != activeClients.end();) {
        if (currentTime - it->second.lastActivity >= TIMEOUT_SEC) {
            printf("Client %d (%s:%d) timed out.\n", it->first, it->second.ipAddress.c_str(), it->second.portNumber);
            it = activeClients.erase(it);
        } else {
            ++it;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <hostname:port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    initCalcLib();

    printf("Starting server...\n");

    char *hostName = strtok(argv[1], ":");
    char *portString = strtok(NULL, ":");

    if (!portString) {
        fprintf(stderr, "Error: Invalid host:port format.\n");
        exit(EXIT_FAILURE);
    }

    struct addrinfo hints = {}, *serverInfo, *p;
    int serverSocket;
    struct sockaddr_storage clientAddr;
    socklen_t addrLen = sizeof(clientAddr);
    char buffer[MAXBUFLEN];

    hints.ai_family = AF_UNSPEC; // Support both IPv4 and IPv6
    hints.ai_socktype = SOCK_DGRAM; // UDP
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(hostName, portString, &hints, &serverInfo) != 0) {
        perror("getaddrinfo");
        exit(EXIT_FAILURE);
    }

    for (p = serverInfo; p != NULL; p = p->ai_next) {
        if ((serverSocket = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("socket");
            continue;
        }

        if (bind(serverSocket, p->ai_addr, p->ai_addrlen) == -1) {
            close(serverSocket);
            perror("bind");
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "Failed to bind socket.\n");
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(serverInfo);

    printf("Server is ready.\n");

    while (true) {
        cleanupTimedOutClients();

        memset(buffer, 0, sizeof(buffer));
        ssize_t receivedBytes = recvfrom(serverSocket, buffer, MAXBUFLEN - 1, 0, (struct sockaddr *)&clientAddr, &addrLen);

        if (receivedBytes == -1) {
            perror("recvfrom");
            continue;
        }

        char clientIP[INET6_ADDRSTRLEN];
        inet_ntop(clientAddr.ss_family,
                  clientAddr.ss_family == AF_INET
                      ? (void *)&(((struct sockaddr_in *)&clientAddr)->sin_addr)
                      : (void *)&(((struct sockaddr_in6 *)&clientAddr)->sin6_addr),
                  clientIP, sizeof(clientIP));
        int clientPort = ntohs(((struct sockaddr_in *)&clientAddr)->sin_port);

        printf("Message received from %s:%d\n", clientIP, clientPort);

        if (receivedBytes == sizeof(calcMessage)) {
            struct calcMessage clientMsg;
            memcpy(&clientMsg, buffer, sizeof(clientMsg));

            clientMsg.type = ntohs(clientMsg.type);
            clientMsg.message = ntohl(clientMsg.message);
            clientMsg.protocol = ntohs(clientMsg.protocol);
            clientMsg.major_version = ntohs(clientMsg.major_version);
            clientMsg.minor_version = ntohs(clientMsg.minor_version);

            if (clientMsg.type != PROTOCOL_TYPE || clientMsg.message != PROTOCOL_MESSAGE ||
                clientMsg.protocol != 17 || clientMsg.major_version != PROTOCOL_VERSION_MAJOR ||
                clientMsg.minor_version != PROTOCOL_VERSION_MINOR) {
                sendResponse(serverSocket, clientAddr, addrLen, RESPONSE_NOT_OK);
                printf("Invalid protocol message from %s:%d\n", clientIP, clientPort);
                continue;
            }

            string operation = randomType();
            calcProtocol newTask = {};
            newTask.type = htonl(1);
            newTask.major_version = htonl(PROTOCOL_VERSION_MAJOR);
            newTask.minor_version = htonl(PROTOCOL_VERSION_MINOR);
            newTask.id = htonl(nextClientID);
            newTask.arith = htonl(getArithIndex(operation));

            if (operation[0] == 'f') {
                newTask.flValue1 = randomFloat();
                newTask.flValue2 = randomFloat();
            } else {
                newTask.inValue1 = htonl(randomInt());
                newTask.inValue2 = htonl(randomInt());
            }

            activeClients[nextClientID] = ClientData(nextClientID, clientIP, clientPort, newTask);

            if (sendto(serverSocket, &newTask, sizeof(newTask), 0, (struct sockaddr *)&clientAddr, addrLen) == -1) {
                perror("sendto");
            } else {
                printf("Sent calculation task to client %d\n", nextClientID);
                nextClientID++;
            }
        } else if (receivedBytes == sizeof(calcProtocol)) {
            struct calcProtocol clientResponse;
            memcpy(&clientResponse, buffer, sizeof(clientResponse));

            int clientID = ntohl(clientResponse.id);

            if (activeClients.find(clientID) == activeClients.end()) {
                sendResponse(serverSocket, clientAddr, addrLen, RESPONSE_NOT_OK);
                printf("Client %s:%d with invalid ID %d tried to respond.\n", clientIP, clientPort, clientID);
                continue;
            }

            ClientData &client = activeClients[clientID];
            if (client.ipAddress != clientIP || client.portNumber != clientPort) {
                sendResponse(serverSocket, clientAddr, addrLen, RESPONSE_NOT_OK);
                printf("Client %s:%d tried to spoof ID %d.\n", clientIP, clientPort, clientID);
                continue;
            }

            printf("Valid response from client %d (%s:%d)\n", clientID, client.ipAddress.c_str(), client.portNumber);
            sendResponse(serverSocket, clientAddr, addrLen, RESPONSE_OK);
            activeClients.erase(clientID);
        }
    }

    close(serverSocket);
    return 0;
}
