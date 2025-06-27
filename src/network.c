// src/network.c
#include "../include/network.h"
#include "../include/logging.h"

int init_server_socket(int port) {
    int server_socket;
    
    // Create server socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        log_message(FTPLOG_ERROR, "Failed to create socket: %s", strerror(errno));
        return -1;
    }
    
    // Set socket options
    int reuse = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        log_message(FTPLOG_ERROR, "Failed to set socket options: %s", strerror(errno));
        close(server_socket);
        return -1;
    }
    
    // Bind to port
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        log_message(FTPLOG_ERROR, "Failed to bind to port %d: %s", port, strerror(errno));
        close(server_socket);
        return -1;
    }
    
    // Listen for connections
    if (listen(server_socket, 5) < 0) {
        log_message(FTPLOG_ERROR, "Failed to listen on port %d: %s", port, strerror(errno));
        close(server_socket);
        return -1;
    }
    
    return server_socket;
}

int open_data_connection(client_t *client) {
    int data_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (data_socket < 0) {
        log_message(FTPLOG_ERROR, "Failed to create data socket: %s", strerror(errno));
        return -1;
    }
    
    struct sockaddr_in data_addr;
    memset(&data_addr, 0, sizeof(data_addr));
    data_addr.sin_family = AF_INET;
    data_addr.sin_addr.s_addr = INADDR_ANY;
    data_addr.sin_port = 0;  // Let the system assign a port
    
    if (bind(data_socket, (struct sockaddr*)&data_addr, sizeof(data_addr)) < 0) {
        log_message(FTPLOG_ERROR, "Failed to bind data socket: %s", strerror(errno));
        close(data_socket);
        return -1;
    }
    
    if (listen(data_socket, 1) < 0) {
        log_message(FTPLOG_ERROR, "Failed to listen on data socket: %s", strerror(errno));
        close(data_socket);
        return -1;
    }
    
    // Get the port assigned by the system
    socklen_t len = sizeof(data_addr);
    if (getsockname(data_socket, (struct sockaddr*)&data_addr, &len) < 0) {
        log_message(FTPLOG_ERROR, "Failed to get socket name: %s", strerror(errno));
        close(data_socket);
        return -1;
    }
    
    int port = ntohs(data_addr.sin_port);
    log_message(FTPLOG_DEBUG, "Data socket listening on port %d", port);
    
    // Get the server's IP address as seen by the client
    struct sockaddr_in server_addr;
    len = sizeof(server_addr);
    if (getsockname(client->control_socket, (struct sockaddr*)&server_addr, &len) < 0) {
        log_message(FTPLOG_ERROR, "Failed to get server IP address: %s", strerror(errno));
        close(data_socket);
        return -1;
    }
    
    // Extract IP address components
    unsigned char *ip = (unsigned char *)&server_addr.sin_addr.s_addr;
    
    // Format: 227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)
    char response[MAX_BUFFER];
    snprintf(response, sizeof(response), 
            "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)\r\n",
            ip[0], ip[1], ip[2], ip[3], port >> 8, port & 0xFF);
    
    send(client->control_socket, response, strlen(response), 0);
    log_message(FTPLOG_DEBUG, "Sent: %s", response);
    
    return data_socket;
}

int create_data_connection(client_t *client) {
    // Create a socket for outgoing connection
    int data_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (data_socket < 0) {
        log_message(FTPLOG_ERROR, "Failed to create data socket for PORT: %s", strerror(errno));
        return -1;
    }
    
    // Set up the address to connect to
    struct sockaddr_in data_addr;
    memset(&data_addr, 0, sizeof(data_addr));
    data_addr.sin_family = AF_INET;
    data_addr.sin_port = htons(client->data_port);
    
    log_message(FTPLOG_DEBUG, "Attempting to connect to %s:%d for data transfer", 
                client->data_ip, client->data_port);
    
    // Convert the IP address string to binary form
    if (inet_pton(AF_INET, client->data_ip, &data_addr.sin_addr) <= 0) {
        log_message(FTPLOG_ERROR, "Invalid IP address in PORT command: %s", client->data_ip);
        close(data_socket);
        return -1;
    }
    
    // Set socket to non-blocking mode for connect with timeout
    int flags = fcntl(data_socket, F_GETFL, 0);
    fcntl(data_socket, F_SETFL, flags | O_NONBLOCK);
    
    // Attempt to connect
    int connect_result = connect(data_socket, (struct sockaddr*)&data_addr, sizeof(data_addr));
    if (connect_result < 0 && errno != EINPROGRESS) {
        log_message(FTPLOG_ERROR, "Failed to start connection to client data port: %s", strerror(errno));
        close(data_socket);
        return -1;
    }
    
    // Set up select for timeout
    fd_set write_fds;
    struct timeval timeout;
    
    FD_ZERO(&write_fds);
    FD_SET(data_socket, &write_fds);
    
    // Set timeout to 5 seconds
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    
    // Wait for connection to complete or timeout
    int select_result = select(data_socket + 1, NULL, &write_fds, NULL, &timeout);
    
    if (select_result <= 0) {
        if (select_result == 0) {
            log_message(FTPLOG_ERROR, "Connection to client data port timed out");
        } else {
            log_message(FTPLOG_ERROR, "Select failed during connection: %s", strerror(errno));
        }
        close(data_socket);
        return -1;
    }
    
    // Check if connection succeeded
    int error = 0;
    socklen_t error_len = sizeof(error);
    if (getsockopt(data_socket, SOL_SOCKET, SO_ERROR, &error, &error_len) < 0 || error != 0) {
        if (error != 0) {
            log_message(FTPLOG_ERROR, "Connection to client data port failed: %s", strerror(error));
        } else {
            log_message(FTPLOG_ERROR, "Failed to get socket error: %s", strerror(errno));
        }
        close(data_socket);
        return -1;
    }
    
    // Set socket back to blocking mode
    fcntl(data_socket, F_SETFL, flags);
    
    log_message(FTPLOG_DEBUG, "Successfully connected to client at %s:%d", 
                client->data_ip, client->data_port);
    
    return data_socket;
}
