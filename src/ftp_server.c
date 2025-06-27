// src/ftp_server.c
#include "config.h"
#include "client.h"
#include "logging.h"
#include "network.h"
#include "utils.h"
#include "commands.h"
#include "daemon.h"

// Global variables
int server_running = 1;
int server_socket = -1;
char root_directory[PATH_MAX];
char upload_directory[PATH_MAX]; // Custom upload directory

// Signal handler for graceful shutdown
void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        log_message(FTPLOG_INFO, "Received signal %d. Shutting down server...", sig);
        server_running = 0;
        
        // Close the server socket first
        if (server_socket >= 0) {
            close(server_socket);
            server_socket = -1;
        }
    }
}

// Clean up resources
void cleanup(void) {
    if (server_socket >= 0) {
        close(server_socket);
    }
    
    client_cleanup();
    
    // Destroy mutexes
    pthread_mutex_destroy(&clients_mutex);
    pthread_mutex_destroy(&log_mutex);
    
    // Close log file
    log_close();
    
    log_message(FTPLOG_INFO, "Server shutdown complete");
}

void print_usage(const char *program_name) {
    fprintf(stderr, "Usage: %s [-d directory] [-u upload_dir] [-t timeout] [-c max_clients] [-D]\n", program_name);
    fprintf(stderr, "  -d directory    Set the root directory for FTP access\n");
    fprintf(stderr, "  -u upload_dir   Set custom upload directory (default: same as root)\n");
    fprintf(stderr, "  -t timeout      Set client inactivity timeout in seconds (default: %d)\n", DEFAULT_CLIENT_TIMEOUT);
    fprintf(stderr, "  -c max_clients  Set maximum number of concurrent clients (default: %d)\n", DEFAULT_MAX_CLIENTS);
    fprintf(stderr, "  -D              Run as daemon (detach from terminal and log to file)\n");
    fprintf(stderr, "  -h              Display this help message\n");
}


int main(int argc, char **argv) {
    int opt;
    char *directory = NULL;
    char *upload_dir = NULL;
    
    // Get program name (without path)
    char *program_name = strrchr(argv[0], '/');
    if (program_name) {
        program_name++; // Skip the slash
    } else {
        program_name = argv[0];
    }
    
    // Initialize logging
    log_init();
    
    // Parse command line arguments
    while ((opt = getopt(argc, argv, "d:u:t:c:Dh")) != -1) {
        switch (opt) {
            case 'd':
                directory = optarg;
                break;
            case 'u':
                upload_dir = optarg;
                break;
            case 't':
                client_timeout = atoi(optarg);
                if (client_timeout <= 0) {
                    fprintf(stderr, "Invalid timeout value. Using default: %d seconds\n", DEFAULT_CLIENT_TIMEOUT);
                    client_timeout = DEFAULT_CLIENT_TIMEOUT;
                }
                break;
            case 'c':
                max_clients = atoi(optarg);
                if (max_clients <= 0 || max_clients > 10000) {
                    fprintf(stderr, "Invalid max clients value. Using default: %d\n", DEFAULT_MAX_CLIENTS);
                    max_clients = DEFAULT_MAX_CLIENTS;
                }
                break;
            case 'D':
                daemon_mode = 1;
                break;
            case 'h':
                print_usage(program_name);
                exit(EXIT_SUCCESS);
            default:
                print_usage(program_name);
                exit(EXIT_FAILURE);
        }
    }
    
    // If daemon mode, daemonize and setup file logging
    if (daemon_mode) {
        // Setup file logging before daemonizing
        if (!log_init_file(program_name)) {
            fprintf(stderr, "Failed to initialize log file. Exiting.\n");
            exit(EXIT_FAILURE);
        }
        
        log_message(FTPLOG_INFO, "Starting in daemon mode...");
        
        // Daemonize the process
        if (!daemonize()) {
            log_message(FTPLOG_ERROR, "Failed to daemonize. Exiting.");
            log_close();
            exit(EXIT_FAILURE);
        }
        
        log_message(FTPLOG_INFO, "Successfully daemonized with PID %d", getpid());
    }
    
    log_message(FTPLOG_INFO, "Client inactivity timeout set to %d seconds", client_timeout);
    log_message(FTPLOG_INFO, "Maximum concurrent clients set to %d", max_clients);
    
    // Set up directory
    if (directory == NULL) {
        if (getcwd(root_directory, sizeof(root_directory)) == NULL) {
            log_message(FTPLOG_ERROR, "Failed to get current directory: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    } else {
        struct stat st;
        if (stat(directory, &st) != 0 || !S_ISDIR(st.st_mode)) {
            log_message(FTPLOG_ERROR, "Error: %s is not a directory", directory);
            exit(EXIT_FAILURE);
        }
        realpath(directory, root_directory);
    }
    
    // Set up upload directory
    if (upload_dir == NULL) {
        // If not specified, use root directory
        strcpy(upload_directory, root_directory);
    } else {
        struct stat st;
        if (stat(upload_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
            log_message(FTPLOG_ERROR, "Error: %s is not a directory", upload_dir);
            exit(EXIT_FAILURE);
        }
        
        // Check if upload directory is writable
        if (access(upload_dir, W_OK) != 0) {
            log_message(FTPLOG_ERROR, "Error: %s is not writable", upload_dir);
            exit(EXIT_FAILURE);
        }
        
        realpath(upload_dir, upload_directory);
    }
    
    log_message(FTPLOG_INFO, "Starting FTP server with root directory: %s", root_directory);
    log_message(FTPLOG_INFO, "Upload directory set to: %s", upload_directory);
    
    // Set up signal handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    // Initialize client module
    client_init();
    
    // Create server socket
    server_socket = init_server_socket(FTP_PORT);
    if (server_socket < 0) {
        exit(EXIT_FAILURE);
    }
    
    log_message(FTPLOG_INFO, "Server listening on port %d", FTP_PORT);
    
    // Variables for timeout checking
    time_t last_timeout_check = time(NULL);
    
    // Main server loop
    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        // Set a timeout on accept to allow for checking server_running
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_socket, &readfds);
        
        struct timeval timeout;
        timeout.tv_sec = 1;  // Check for shutdown every second
        timeout.tv_usec = 0;
        
        int select_result = select(server_socket + 1, &readfds, NULL, NULL, &timeout);
        
        // Check for inactive clients every 60 seconds
        time_t current_time = time(NULL);
        if (difftime(current_time, last_timeout_check) >= 60) {
            check_inactive_clients();
            last_timeout_check = current_time;
            
            // Log current client count
            log_message(FTPLOG_INFO, "Active clients: %d/%d", active_clients, max_clients);
        }
        
        if (select_result <= 0) {
            if (select_result < 0 && errno != EINTR) {
                log_message(FTPLOG_ERROR, "Select failed: %s", strerror(errno));
            }
            continue;  // Timeout or error, check server_running and try again
        }
        
        // New connection available
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_socket < 0) {
            if (errno == EINTR || errno == EBADF) {
                // Interrupted by signal or server socket closed
                continue;
            }
            log_message(FTPLOG_ERROR, "Failed to accept client connection: %s", strerror(errno));
            continue;
        }
        
        // Create client structure
        client_t *client = (client_t*)malloc(sizeof(client_t));
        if (!client) {
            log_message(FTPLOG_ERROR, "Failed to allocate memory for client: %s", strerror(errno));
            close(client_socket);
            continue;
        }
        
        memset(client, 0, sizeof(client_t));
        client->control_socket = client_socket;
        client->data_socket = -1;
        client->transfer_mode = TRANSFER_MODE_NONE;
        client->thread_running = 1;  // Set thread as running
        inet_ntop(AF_INET, &client_addr.sin_addr, client->ip_address, sizeof(client->ip_address));
        
        // Check if we've reached max clients
        if (!add_client(client)) {
            log_message(FTPLOG_ERROR, "Maximum number of clients reached (%d). Rejecting connection from %s", 
                      max_clients, client->ip_address);
            send_response(client->control_socket, 421, "Service not available, too many users connected");
            close(client_socket);
            free(client);
            continue;
        }
        
        log_message(FTPLOG_INFO, "New client connected: %s (%d/%d active)", 
                  client->ip_address, active_clients, max_clients);
        
        // Create a thread to handle the client
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        
        if (pthread_create(&client->thread_id, &attr, handle_client_thread, client) != 0) {
            log_message(FTPLOG_ERROR, "Failed to create thread for client: %s", strerror(errno));
            remove_client(client);
            close(client_socket);
            free(client);
        }
        
        pthread_attr_destroy(&attr);
    }
    
    // Wait for all threads to finish
    log_message(FTPLOG_INFO, "Waiting for all client threads to terminate...");
    
    // Set all threads to stop
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < max_clients; i++) {
        if (clients[i]) {
            clients[i]->thread_running = 0;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    
    // Give threads time to exit
    sleep(2);
    
    cleanup();
    return 0;
}
