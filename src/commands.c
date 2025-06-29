// src/commands.c
#include "commands.h"
#include "logging.h"
#include "network.h"

void send_response(int socket, int code, const char *message) {
    char response[MAX_BUFFER];
    snprintf(response, sizeof(response), "%d %s\r\n", code, message);
    send(socket, response, strlen(response), 0);
    log_message(FTPLOG_DEBUG, "Sent: %d %s", code, message);
}

void process_command(client_t *client, const char *command, const char *arg) {
    // Update activity timestamp for each command
    client_update_activity(client);
    
    if (strcmp(command, "USER") == 0) {
        send_response(client->control_socket, 331, "User name okay, need password");
    }
    else if (strcmp(command, "PASS") == 0) {
        send_response(client->control_socket, 230, "User logged in, proceed");
    }
    else if (strcmp(command, "SYST") == 0) {
        send_response(client->control_socket, 215, "UNIX Type: L8");
    }
    else if (strcmp(command, "FEAT") == 0) {
        // List supported features
        char response[MAX_BUFFER];
        strcpy(response, "211-Features:\r\n");
        strcat(response, " UTF8\r\n");
        strcat(response, " PASV\r\n");
        strcat(response, "211 End\r\n");
        send(client->control_socket, response, strlen(response), 0);
    }
    else if (strcmp(command, "OPTS") == 0) {
        // Handle options command
        if (strncmp(arg, "UTF8", 4) == 0) {
            send_response(client->control_socket, 200, "UTF8 option accepted");
        } else {
            send_response(client->control_socket, 501, "Option not supported");
        }
    }
    else if (strcmp(command, "PWD") == 0) {
        // Get current directory relative to root
        char rel_path[PATH_MAX] = {0};
        
        // Calculate relative path
        if (strlen(client->current_dir) < strlen(root_directory)) {
            // This should never happen, but just in case
            strcpy(rel_path, "/");
        } else if (strlen(client->current_dir) == strlen(root_directory)) {
            // At root directory
            strcpy(rel_path, "/");
        } else {
            // Format properly with leading slash
            snprintf(rel_path, sizeof(rel_path), "%s", client->current_dir + strlen(root_directory));
            if (rel_path[0] != '/') {
                memmove(rel_path + 1, rel_path, strlen(rel_path) + 1);
                rel_path[0] = '/';
            }
        }
        
        // Log for debugging
        log_message(FTPLOG_DEBUG, "PWD: root_directory=%s", root_directory);
        log_message(FTPLOG_DEBUG, "PWD: current_dir=%s", client->current_dir);
        log_message(FTPLOG_DEBUG, "PWD: reporting=%s", rel_path);
        
        // Send the response - note that FTP requires double quotes around the path
        char response[MAX_BUFFER];
        snprintf(response, sizeof(response), "257 \"%s\" is current directory\r\n", rel_path);
        send(client->control_socket, response, strlen(response), 0);
        log_message(FTPLOG_DEBUG, "Sent: 257 \"%s\" is current directory", rel_path);
    }
    else if (strcmp(command, "CWD") == 0) {
        char new_path[PATH_MAX];
        char normalized_path[PATH_MAX];
        
        // Handle different path formats
        if (strlen(arg) == 0) {
            // Empty argument - do nothing
            send_response(client->control_socket, 250, "Directory successfully changed");
            return;
        }
        else if (strcmp(arg, "/") == 0) {
            // Root directory
            strcpy(client->current_dir, root_directory);
            send_response(client->control_socket, 250, "Directory successfully changed");
            return;
        }
        else if (arg[0] == '/') {
            // Absolute path (relative to FTP root)
            snprintf(new_path, sizeof(new_path), "%s%s", root_directory, arg);
        } 
        else if (strcmp(arg, "..") == 0) {
            // Parent directory
            char *last_slash = strrchr(client->current_dir, '/');
            if (last_slash != NULL && last_slash > client->current_dir) {
                *last_slash = '\0';  // Remove last path component
                
                // Make sure we don't go above root directory
                if (strlen(client->current_dir) < strlen(root_directory)) {
                    strcpy(client->current_dir, root_directory);
                }
                
                send_response(client->control_socket, 250, "Directory successfully changed");
                return;
            } else {
                // Already at root or no slash found
                strcpy(client->current_dir, root_directory);
                send_response(client->control_socket, 250, "Directory successfully changed");
                return;
            }
        }
        else {
            // Relative path
            snprintf(new_path, sizeof(new_path), "%s/%s", client->current_dir, arg);
        }
        
        // Log for debugging
        log_message(FTPLOG_DEBUG, "CWD: Requested path: %s", arg);
        log_message(FTPLOG_DEBUG, "CWD: Constructed path: %s", new_path);
        
        // Normalize the path (resolve .., ., and symlinks)
        if (realpath(new_path, normalized_path) == NULL) {
            log_message(FTPLOG_ERROR, "CWD: Invalid path: %s (%s)", new_path, strerror(errno));
            send_response(client->control_socket, 550, "Failed to change directory");
            return;
        }
        
        // Ensure the path is within the allowed root directory
        if (strncmp(normalized_path, root_directory, strlen(root_directory)) != 0) {
            log_message(FTPLOG_ERROR, "CWD: Path outside root directory: %s", normalized_path);
            send_response(client->control_socket, 550, "Access denied");
            return;
        }
        
        // Check if directory exists and is accessible
        struct stat st;
        if (stat(normalized_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            strcpy(client->current_dir, normalized_path);
            log_message(FTPLOG_DEBUG, "CWD: Changed to: %s", normalized_path);
            send_response(client->control_socket, 250, "Directory successfully changed");
        } else {
            log_message(FTPLOG_ERROR, "CWD: Directory not accessible: %s (%s)", normalized_path, strerror(errno));
            send_response(client->control_socket, 550, "Failed to change directory");
        }
    }
    else if (strcmp(command, "TYPE") == 0) {
        // We support both ASCII and Binary mode
        if (arg[0] == 'A') {
            send_response(client->control_socket, 200, "Type set to A");
        } else if (arg[0] == 'I') {
            send_response(client->control_socket, 200, "Type set to I");
        } else {
            send_response(client->control_socket, 504, "Type not supported");
        }
    }
    else if (strcmp(command, "PORT") == 0) {
        // Close any existing data socket
        if (client->data_socket >= 0) {
            close(client->data_socket);
            client->data_socket = -1;
        }
        
        // Parse PORT command arguments (h1,h2,h3,h4,p1,p2)
        unsigned int h1, h2, h3, h4, p1, p2;
        if (sscanf(arg, "%u,%u,%u,%u,%u,%u", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
            send_response(client->control_socket, 501, "Invalid PORT command");
            return;
        }
        
        // Ensure valid IP components
        if (h1 > 255 || h2 > 255 || h3 > 255 || h4 > 255 || p1 > 255 || p2 > 255) {
            send_response(client->control_socket, 501, "Invalid PORT command arguments");
            return;
        }
        
        // Calculate port number
        client->data_port = (p1 << 8) + p2;
        
        // IMPORTANT FIX: Use the actual client IP address from the control connection
        // instead of the potentially invalid one provided in the PORT command
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        if (getpeername(client->control_socket, (struct sockaddr*)&addr, &addr_len) < 0) {
            log_message(FTPLOG_ERROR, "Failed to get client IP address: %s", strerror(errno));
            send_response(client->control_socket, 501, "Cannot process PORT command");
            return;
        }
        
        // Get the client's actual IP address
        inet_ntop(AF_INET, &(addr.sin_addr), client->data_ip, sizeof(client->data_ip));
        
        log_message(FTPLOG_DEBUG, "PORT: Client data connection set to %s:%d (original IP in command: %u.%u.%u.%u)", 
                client->data_ip, client->data_port, h1, h2, h3, h4);
        
        // Set client to active mode
        client->transfer_mode = TRANSFER_MODE_PORT;
        
        send_response(client->control_socket, 200, "PORT command successful");
    }
    else if (strcmp(command, "PASV") == 0) {
        // Close any existing data socket
        if (client->data_socket >= 0) {
            close(client->data_socket);
            client->data_socket = -1;
        }
        
        int data_socket = open_data_connection(client);
        if (data_socket >= 0) {
            client->data_socket = data_socket;
            client->transfer_mode = TRANSFER_MODE_PASV;
        } else {
            send_response(client->control_socket, 425, "Cannot open data connection");
        }
    }
    else if (strcmp(command, "LIST") == 0 || strcmp(command, "NLST") == 0) {
        int data_conn = -1;
        
        // Check if transfer mode is set
        if (client->transfer_mode == TRANSFER_MODE_NONE) {
            send_response(client->control_socket, 425, "Use PORT or PASV first");
            return;
        }
        
        // Set up data connection based on transfer mode
        if (client->transfer_mode == TRANSFER_MODE_PORT) {
            // Active mode - we connect to the client
            data_conn = create_data_connection(client);
            if (data_conn < 0) {
                send_response(client->control_socket, 425, "Cannot open data connection");
                return;
            }
        } else {
            // Passive mode - accept connection from client
            if (client->data_socket < 0) {
                send_response(client->control_socket, 425, "Cannot open data connection");
                return;
            }
            
            // Tell client we're ready to send directory listing
            send_response(client->control_socket, 150, "Here comes the directory listing");
            
            // Accept the connection from client
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            data_conn = accept(client->data_socket, (struct sockaddr*)&client_addr, &client_len);
            
            if (data_conn < 0) {
                log_message(FTPLOG_ERROR, "Failed to accept data connection: %s", strerror(errno));
                send_response(client->control_socket, 425, "Cannot open data connection");
                close(client->data_socket);
                client->data_socket = -1;
                return;
            }
        }
        
        // In active mode, send the 150 message after the connection is established
        if (client->transfer_mode == TRANSFER_MODE_PORT) {
            send_response(client->control_socket, 150, "Here comes the directory listing");
        }
        
        // Open directory
        DIR *dir = opendir(client->current_dir);
        if (dir == NULL) {
            log_message(FTPLOG_ERROR, "Failed to open directory: %s", strerror(errno));
            send_response(client->control_socket, 550, "Failed to open directory");
            close(data_conn);
            if (client->transfer_mode == TRANSFER_MODE_PASV) {
                close(client->data_socket);
                client->data_socket = -1;
            }
            return;
        }
        
        struct dirent *entry;
        char line[MAX_BUFFER];
        
        // Read directory entries
        while ((entry = readdir(dir)) != NULL) {
            char full_path[PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", client->current_dir, entry->d_name);
            
            struct stat st;
            if (stat(full_path, &st) == 0) {
                if (strcmp(command, "LIST") == 0) {
                    // Format like ls -l
                    char perms[11];
                    perms[0] = S_ISDIR(st.st_mode) ? 'd' : '-';
                    perms[1] = (st.st_mode & S_IRUSR) ? 'r' : '-';
                    perms[2] = (st.st_mode & S_IWUSR) ? 'w' : '-';
                    perms[3] = (st.st_mode & S_IXUSR) ? 'x' : '-';
                    perms[4] = (st.st_mode & S_IRGRP) ? 'r' : '-';
                    perms[5] = (st.st_mode & S_IWGRP) ? 'w' : '-';
                    perms[6] = (st.st_mode & S_IXGRP) ? 'x' : '-';
                    perms[7] = (st.st_mode & S_IROTH) ? 'r' : '-';
                    perms[8] = (st.st_mode & S_IWOTH) ? 'w' : '-';
                    perms[9] = (st.st_mode & S_IXOTH) ? 'x' : '-';
                    perms[10] = '\0';
                    
                    char time_str[20];
                    strftime(time_str, sizeof(time_str), "%b %d %H:%M", localtime(&st.st_mtime));
                    
                    snprintf(line, sizeof(line), "%s %3d %-8d %-8d %8lld %s %s\r\n",
                            perms, (int)st.st_nlink, (int)st.st_uid, (int)st.st_gid,
                            (long long)st.st_size, time_str, entry->d_name);
                } else {
                    // Just filename for NLST
                    snprintf(line, sizeof(line), "%s\r\n", entry->d_name);
                }
                
                if (send(data_conn, line, strlen(line), 0) < 0) {
                    log_message(FTPLOG_ERROR, "Failed to send directory entry: %s", strerror(errno));
                    break;
                }
                
                // Update activity timestamp during transfer to prevent timeout
                client_update_activity(client);
            }
        }
        
        closedir(dir);
        close(data_conn);
        
        if (client->transfer_mode == TRANSFER_MODE_PASV) {
            close(client->data_socket);
            client->data_socket = -1;
        }
        
        send_response(client->control_socket, 226, "Directory send OK");
    }
    else if (strcmp(command, "RETR") == 0) {
        int data_conn = -1;
        
        // Check if transfer mode is set
        if (client->transfer_mode == TRANSFER_MODE_NONE) {
            send_response(client->control_socket, 425, "Use PORT or PASV first");
            return;
        }
        
        // Build full path
        char file_path[PATH_MAX];
        if (arg[0] == '/') {
            snprintf(file_path, sizeof(file_path), "%s%s", root_directory, arg);
        } else {
            snprintf(file_path, sizeof(file_path), "%s/%s", client->current_dir, arg);
        }
        
        // Open file
        int file_fd = open(file_path, O_RDONLY);
        if (file_fd < 0) {
            log_message(FTPLOG_ERROR, "Failed to open file: %s - %s", file_path, strerror(errno));
            send_response(client->control_socket, 550, "Failed to open file");
            return;
        }
        
        // Get file size
        struct stat st;
        fstat(file_fd, &st);
        
        // Set up data connection based on transfer mode
        if (client->transfer_mode == TRANSFER_MODE_PORT) {
            // Active mode - we connect to the client
            data_conn = create_data_connection(client);
            if (data_conn < 0) {
                close(file_fd);
                send_response(client->control_socket, 425, "Cannot open data connection");
                return;
            }
            
            // Send 150 response after connection is established
            send_response(client->control_socket, 150, "Opening BINARY mode data connection for file transfer");
        } else {
            // Passive mode - accept connection from client
            if (client->data_socket < 0) {
                close(file_fd);
                send_response(client->control_socket, 425, "Cannot open data connection");
                return;
            }
            
            // Tell client we're ready to send file
            send_response(client->control_socket, 150, "Opening BINARY mode data connection for file transfer");
            
            // Accept the connection from client
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            data_conn = accept(client->data_socket, (struct sockaddr*)&client_addr, &client_len);
            
            if (data_conn < 0) {
                log_message(FTPLOG_ERROR, "Failed to accept data connection: %s", strerror(errno));
                send_response(client->control_socket, 425, "Cannot open data connection");
                close(file_fd);
                close(client->data_socket);
                client->data_socket = -1;
                return;
            }
        }
        
        // Transfer file
        char buffer[8192];
        ssize_t bytes;
        size_t total_bytes = 0;
        time_t start_time = time(NULL);
        time_t last_log = start_time;
        
        while ((bytes = read(file_fd, buffer, sizeof(buffer))) > 0) {
            ssize_t sent = send(data_conn, buffer, bytes, 0);
            if (sent <= 0) {
                log_message(FTPLOG_ERROR, "Failed to send file data: %s", strerror(errno));
                break;
            }
            
            total_bytes += sent;
            time_t current_time = time(NULL);
            
            // Update activity timestamp during transfer to prevent timeout
            client_update_activity(client);
            
            // Log transfer rate every second
            if (current_time > last_log) {
                double elapsed = difftime(current_time, start_time);
                if (elapsed > 0) {
                    double rate = total_bytes / elapsed;
                    char rate_str[64];
                    format_transfer_rate(rate, rate_str, sizeof(rate_str));
                    log_message(FTPLOG_TRANSFER, "Transferring %s: %zu bytes, %s", 
                                arg, total_bytes, rate_str);
                }
                last_log = current_time;
            }
        }
        
        close(file_fd);
        close(data_conn);
        
        if (client->transfer_mode == TRANSFER_MODE_PASV) {
            close(client->data_socket);
            client->data_socket = -1;
        }
        
        time_t end_time = time(NULL);
        double elapsed = difftime(end_time, start_time);
        double rate = (elapsed > 0) ? (total_bytes / elapsed) : 0;
        char rate_str[64];
        format_transfer_rate(rate, rate_str, sizeof(rate_str));
        
        log_message(FTPLOG_TRANSFER, "Completed transfer of %s: %zu bytes in %.1f seconds, %s", 
                    arg, total_bytes, elapsed, rate_str);
        
        send_response(client->control_socket, 226, "Transfer complete");
    }
    else if (strcmp(command, "STOR") == 0) {
        int data_conn = -1;
        
        // Check if transfer mode is set
        if (client->transfer_mode == TRANSFER_MODE_NONE) {
            send_response(client->control_socket, 425, "Use PORT or PASV first");
            return;
        }
        
        // Build the target file path
        char file_path[PATH_MAX];
        
        if (arg[0] == '/') {
            // Absolute path (relative to FTP root)
            snprintf(file_path, sizeof(file_path), "%s%s", root_directory, arg);
        } else {
            // Relative path
            snprintf(file_path, sizeof(file_path), "%s/%s", client->current_dir, arg);
        }
        
        // Get the directory part of the path
        char dir_path[PATH_MAX];
        strcpy(dir_path, file_path);
        char *last_slash = strrchr(dir_path, '/');
        if (last_slash != NULL) {
            *last_slash = '\0';  // Truncate at the last slash
        }
        
        // Check if the directory exists and is writable
        struct stat st;
        if (stat(dir_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
            log_message(FTPLOG_ERROR, "STOR: Directory does not exist: %s", dir_path);
            send_response(client->control_socket, 550, "Directory does not exist");
            return;
        }
        
        // Check if the directory is writable
        if (access(dir_path, W_OK) != 0) {
            log_message(FTPLOG_ERROR, "STOR: Directory not writable: %s", dir_path);
            send_response(client->control_socket, 550, "Permission denied");
            return;
        }
        
        // Open the file for writing
        int file_fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (file_fd < 0) {
            log_message(FTPLOG_ERROR, "STOR: Failed to create file: %s - %s", file_path, strerror(errno));
            send_response(client->control_socket, 550, "Failed to create file");
            return;
        }
        
        log_message(FTPLOG_DEBUG, "STOR: Creating file: %s", file_path);
        
        // Set up data connection based on transfer mode
        if (client->transfer_mode == TRANSFER_MODE_PORT) {
            // Active mode - we connect to the client
            data_conn = create_data_connection(client);
            if (data_conn < 0) {
                close(file_fd);
                send_response(client->control_socket, 425, "Cannot open data connection");
                return;
            }
            
            // Send 150 response after connection is established
            send_response(client->control_socket, 150, "Opening BINARY mode data connection for file transfer");
        } else {
            // Passive mode - accept connection from client
            if (client->data_socket < 0) {
                close(file_fd);
                send_response(client->control_socket, 425, "Cannot open data connection");
                return;
            }
            
            // Tell client we're ready to receive file
            send_response(client->control_socket, 150, "Opening BINARY mode data connection for file transfer");
            
            // Accept the connection from client
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            data_conn = accept(client->data_socket, (struct sockaddr*)&client_addr, &client_len);
            
            if (data_conn < 0) {
                log_message(FTPLOG_ERROR, "STOR: Failed to accept data connection: %s", strerror(errno));
                send_response(client->control_socket, 425, "Cannot open data connection");
                close(file_fd);
                close(client->data_socket);
                client->data_socket = -1;
                return;
            }
        }
        
        // Receive file data and write to disk
        char buffer[8192];
        ssize_t bytes;
        size_t total_bytes = 0;
        time_t start_time = time(NULL);
        time_t last_log = start_time;
        
        while ((bytes = recv(data_conn, buffer, sizeof(buffer), 0)) > 0) {
            ssize_t written = write(file_fd, buffer, bytes);
            if (written < 0) {
                log_message(FTPLOG_ERROR, "STOR: Failed to write to file: %s", strerror(errno));
                break;
            }
            
            total_bytes += written;
            time_t current_time = time(NULL);
            
            // Update activity timestamp during transfer to prevent timeout
            client_update_activity(client);
            
            // Log transfer rate every second
            if (current_time > last_log) {
                double elapsed = difftime(current_time, start_time);
                if (elapsed > 0) {
                    double rate = total_bytes / elapsed;
                    char rate_str[64];
                    format_transfer_rate(rate, rate_str, sizeof(rate_str));
                    log_message(FTPLOG_TRANSFER, "Receiving %s: %zu bytes, %s", 
                                arg, total_bytes, rate_str);
                }
                last_log = current_time;
            }
        }
        
        // Check for error in recv
        if (bytes < 0 && errno != ECONNRESET) {
            log_message(FTPLOG_ERROR, "STOR: Error receiving data: %s", strerror(errno));
        }
        
        // Close file and data connection
        close(file_fd);
        close(data_conn);
        
        if (client->transfer_mode == TRANSFER_MODE_PASV) {
            close(client->data_socket);
            client->data_socket = -1;
        }
        
        time_t end_time = time(NULL);
        double elapsed = difftime(end_time, start_time);
        double rate = (elapsed > 0) ? (total_bytes / elapsed) : 0;
        char rate_str[64];
        format_transfer_rate(rate, rate_str, sizeof(rate_str));
        
        log_message(FTPLOG_TRANSFER, "Completed receiving %s: %zu bytes in %.1f seconds, %s", 
                    arg, total_bytes, elapsed, rate_str);
        
        send_response(client->control_socket, 226, "Transfer complete");
    }
    else if (strcmp(command, "QUIT") == 0) {
        send_response(client->control_socket, 221, "Goodbye");
    }
    else {
        // Unsupported command
        send_response(client->control_socket, 502, "Command not implemented");
    }
}
