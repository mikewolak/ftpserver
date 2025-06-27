// include/config.h
#ifndef CONFIG_H
#define CONFIG_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <getopt.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <ctype.h>
#include <syslog.h>

// If PATH_MAX is not defined on some systems, define it
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define FTP_PORT 21
#define MAX_BUFFER 1024
#define DEFAULT_MAX_CLIENTS 512  // Default maximum concurrent clients
#define DEFAULT_CLIENT_TIMEOUT 300  // Default inactivity timeout in seconds (5 minutes)
#define DEFAULT_LOG_DIR "/var/log/ftpserver"  // Default log directory

// Global variables
extern int server_running;
extern int server_socket;
extern char root_directory[PATH_MAX];
extern char upload_directory[PATH_MAX];  // Custom upload directory
extern int client_timeout;  // Configurable timeout
extern int max_clients;     // Maximum number of concurrent clients
extern int daemon_mode;     // Flag for daemon mode
extern FILE *log_file;      // Log file handle

// Thread management
extern pthread_mutex_t clients_mutex;  // Mutex for client array access
extern pthread_mutex_t log_mutex;      // Mutex for log file access

#endif // CONFIG_H
