// include/network.h
#ifndef NETWORK_H
#define NETWORK_H

#include "config.h"
#include "client.h"

// Initialize server socket
int init_server_socket(int port);

// Open data connection for passive mode
int open_data_connection(client_t *client);

// Create data connection for active mode
int create_data_connection(client_t *client);

#endif // NETWORK_H
