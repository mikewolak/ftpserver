// include/client.h
#ifndef CLIENT_H
#define CLIENT_H

#include "config.h"

// Client structure for multi-client support
typedef struct {
    int control_socket;
    int data_socket;
    char ip_address[INET6_ADDRSTRLEN];
    char current_dir[PATH_MAX];
    pthread_t thread_id;
    int thread_running;    // Flag to indicate if thread is running
    
    // Data transfer mode
    int transfer_mode;     // 0=not set, 1=PORT (active), 2=PASV (passive)
    
    // For PORT mode
    char data_ip[INET6_ADDRSTRLEN];
    int data_port;
    
    // Activity tracking
    time_t last_activity;  // Timestamp of last activity
} client_t;

// Transfer modes
#define TRANSFER_MODE_NONE 0
#define TRANSFER_MODE_PORT 1
#define TRANSFER_MODE_PASV 2

extern client_t **clients;
extern int active_clients;

// Initialize client module
void client_init(void);

// Cleanup client module
void client_cleanup(void);

// Update client activity timestamp
void client_update_activity(client_t *client);

// Check for inactive clients and disconnect them
void check_inactive_clients(void);

// Add a new client
int add_client(client_t *client);

// Remove a client
void remove_client(client_t *client);

// Thread function for handling a client
void *handle_client_thread(void *arg);

// Disconnect a client
void disconnect_client(client_t *client);

#endif // CLIENT_H
