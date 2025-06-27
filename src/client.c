// src/client.c
#include "client.h"
#include "logging.h"
#include "commands.h"
#include "network.h"

// Global variables
client_t **clients = NULL;
int active_clients = 0;
int client_timeout = DEFAULT_CLIENT_TIMEOUT;  // Default timeout value
int max_clients = DEFAULT_MAX_CLIENTS;        // Default maximum clients
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void client_init(void) {
    // Allocate client array
    clients = (client_t **)calloc(max_clients, sizeof(client_t *));
    if (!clients) {
        log_message(FTPLOG_ERROR, "Failed to allocate memory for client array");
        exit(EXIT_FAILURE);
    }
}

void client_cleanup(void) {
    if (!clients) return;
    
    pthread_mutex_lock(&clients_mutex);
    
    for (int i = 0; i < max_clients; i++) {
        if (clients[i]) {
            disconnect_client(clients[i]);
            free(clients[i]);
            clients[i] = NULL;
        }
    }
    
    active_clients = 0;
    free(clients);
    clients = NULL;
    
    pthread_mutex_unlock(&clients_mutex);
}

void client_update_activity(client_t *client) {
    if (client) {
        client->last_activity = time(NULL);
    }
}

void check_inactive_clients(void) {
    time_t current_time = time(NULL);
    
    pthread_mutex_lock(&clients_mutex);
    
    for (int i = 0; i < max_clients; i++) {
        if (clients[i] && clients[i]->thread_running) {
            // Check if client has been inactive for too long
            if (difftime(current_time, clients[i]->last_activity) > client_timeout) {
                log_message(FTPLOG_INFO, "Client %s timed out after %d seconds of inactivity", 
                           clients[i]->ip_address, client_timeout);
                
                // Send timeout message if socket is still valid
                if (clients[i]->control_socket >= 0) {
                    send_response(clients[i]->control_socket, 421, "Timeout: closing control connection");
                }
                
                // Indicate thread should stop
                clients[i]->thread_running = 0;
                
                // Disconnect will happen in the thread
            }
        }
    }
    
    pthread_mutex_unlock(&clients_mutex);
}

int add_client(client_t *client) {
    int added = 0;
    
    pthread_mutex_lock(&clients_mutex);
    
    // Check if we've reached max clients
    if (active_clients >= max_clients) {
        pthread_mutex_unlock(&clients_mutex);
        return 0;
    }
    
    // Find a free slot
    for (int i = 0; i < max_clients; i++) {
        if (clients[i] == NULL) {
            clients[i] = client;
            active_clients++;
            added = 1;
            break;
        }
    }
    
    pthread_mutex_unlock(&clients_mutex);
    return added;
}

void remove_client(client_t *client) {
    pthread_mutex_lock(&clients_mutex);
    
    for (int i = 0; i < max_clients; i++) {
        if (clients[i] == client) {
            clients[i] = NULL;
            active_clients--;
            break;
        }
    }
    
    pthread_mutex_unlock(&clients_mutex);
}

void disconnect_client(client_t *client) {
    if (!client) return;
    
    // Close data socket if open
    if (client->data_socket >= 0) {
        close(client->data_socket);
        client->data_socket = -1;
    }
    
    // Close control socket
    if (client->control_socket >= 0) {
        close(client->control_socket);
        client->control_socket = -1;
    }
}

void *handle_client_thread(void *arg) {
    client_t *client = (client_t *)arg;
    char buffer[MAX_BUFFER];
    int bytes_read;
    
    // Set client's current directory to root directory
    strcpy(client->current_dir, root_directory);
    log_message(FTPLOG_DEBUG, "Client initial directory set to: %s", client->current_dir);
    
    // Initialize transfer mode and activity timestamp
    client->transfer_mode = TRANSFER_MODE_NONE;
    client->data_socket = -1;
    client_update_activity(client);  // Set initial activity timestamp
    
    // Send welcome message
    send_response(client->control_socket, 220, "Welcome to Simple FTP Server");
    
    // Set up socket receive timeout for clean thread shutdown
    struct timeval tv;
    tv.tv_sec = 1;  // 1 second timeout
    tv.tv_usec = 0;
    setsockopt(client->control_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    
    while (server_running && client->thread_running) {
        // Use recv with timeout to periodically check server_running and thread_running
        bytes_read = recv(client->control_socket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_read > 0) {
            // Update activity timestamp on any received data
            client_update_activity(client);
            
            buffer[bytes_read] = '\0';
            
            // Remove trailing CRLF
            if (bytes_read > 1 && buffer[bytes_read - 2] == '\r' && buffer[bytes_read - 1] == '\n') {
                buffer[bytes_read - 2] = '\0';
            } else if (bytes_read > 0 && buffer[bytes_read - 1] == '\n') {
                buffer[bytes_read - 1] = '\0';
            }
            
            log_message(FTPLOG_DEBUG, "Received from %s: %s", client->ip_address, buffer);
            
            char command[32] = {0};
            char arg[MAX_BUFFER] = {0};
            
            // Extract command and argument
            if (sscanf(buffer, "%31s %[^\n]", command, arg) < 1) {
                continue;
            }
            
            // Convert command to uppercase
            for (int i = 0; command[i]; i++) {
                command[i] = toupper(command[i]);
            }
            
            // Process the command
            process_command(client, command, arg);
            
            // Update activity timestamp after processing command
            client_update_activity(client);
            
            // Check if client wants to quit
            if (strcmp(command, "QUIT") == 0) {
                break;
            }
        } 
        else if (bytes_read == 0) {
            // Client closed connection
            log_message(FTPLOG_INFO, "Client %s closed connection", client->ip_address);
            break;
        }
        else {
            // Error or timeout
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                log_message(FTPLOG_ERROR, "Client %s recv error: %s", 
                          client->ip_address, strerror(errno));
                break;
            }
            // Otherwise it's just a timeout, continue to check if we should still be running
        }
    }
    
    // Clean up client
    log_message(FTPLOG_INFO, "Client disconnected: %s", client->ip_address);
    disconnect_client(client);
    remove_client(client);
    free(client);
    
    return NULL;
}
