// src/logging.c
#include "logging.h"

// Log file
FILE *log_file = NULL;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_init(void) {
    // Nothing to do for console logging
}

int log_init_file(const char *program_name) {
    char log_path[PATH_MAX];
    char timestamp[32];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    // Format timestamp for log filename
    strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", tm_info);
    
    // Check if log directory exists, create if needed
    struct stat st = {0};
    if (stat(DEFAULT_LOG_DIR, &st) == -1) {
        // Try to create the directory
        if (mkdir(DEFAULT_LOG_DIR, 0755) == -1) {
            // If we can't create the default log dir, use /tmp
            snprintf(log_path, sizeof(log_path), "/tmp/%s-%s.log", program_name, timestamp);
        } else {
            snprintf(log_path, sizeof(log_path), "%s/%s-%s.log", DEFAULT_LOG_DIR, program_name, timestamp);
        }
    } else {
        snprintf(log_path, sizeof(log_path), "%s/%s-%s.log", DEFAULT_LOG_DIR, program_name, timestamp);
    }
    
    // Open log file
    log_file = fopen(log_path, "a");
    if (!log_file) {
        fprintf(stderr, "Failed to open log file %s: %s\n", log_path, strerror(errno));
        return 0;
    }
    
    // Set line buffering for more immediate output
    setvbuf(log_file, NULL, _IOLBF, 0);
    
    // Log initialization message
    log_message(FTPLOG_INFO, "Log file opened: %s", log_path);
    
    // Also log to syslog for daemon mode
    openlog(program_name, LOG_PID, LOG_DAEMON);
    syslog(FTPLOG_INFO, "FTP server started in daemon mode, logging to %s", log_path);
    
    return 1;
}

void log_close(void) {
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
    
    // Close syslog
    closelog();
}

void log_message(log_level_t level, const char *format, ...) {
    va_list args;
    char timestamp[20];
    time_t now = time(NULL);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    const char *level_str;
    int syslog_level;
    
    switch (level) {
        case FTPLOG_INFO:     
            level_str = "INFO"; 
            syslog_level = FTPLOG_INFO;
            break;
        case FTPLOG_ERROR:    
            level_str = "ERROR"; 
            syslog_level = LOG_ERR;
            break;
        case FTPLOG_DEBUG:    
            level_str = "DEBUG"; 
            syslog_level = FTPLOG_DEBUG;
            break;
        case FTPLOG_TRANSFER: 
            level_str = "TRANSFER"; 
            syslog_level = FTPLOG_INFO;
            break;
        default:           
            level_str = "UNKNOWN"; 
            syslog_level = FTPLOG_INFO;
            break;
    }
    
    // Format the message
    char full_message[MAX_BUFFER * 2];
    va_start(args, format);
    vsnprintf(full_message, sizeof(full_message), format, args);
    va_end(args);
    
    // Thread-safe logging
    pthread_mutex_lock(&log_mutex);
    
    // Log to file if in daemon mode
    if (log_file) {
        fprintf(log_file, "[%s] [%s] %s\n", timestamp, level_str, full_message);
        fflush(log_file);
    } else {
        // Log to console if not in daemon mode
        fprintf(stderr, "[%s] [%s] %s\n", timestamp, level_str, full_message);
    }
    
    // Also log to syslog in daemon mode (critical messages only)
    if (daemon_mode && (level == FTPLOG_ERROR || level == FTPLOG_INFO)) {
        syslog(syslog_level, "%s", full_message);
    }
    
    pthread_mutex_unlock(&log_mutex);
}

char* format_transfer_rate(double bytes_per_sec, char* buffer, size_t buffer_size) {
    if (bytes_per_sec < 1024) {
        snprintf(buffer, buffer_size, "%.2f bytes/sec", bytes_per_sec);
    } else if (bytes_per_sec < 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.2f KB/sec", bytes_per_sec / 1024);
    } else {
        snprintf(buffer, buffer_size, "%.2f MB/sec", bytes_per_sec / (1024 * 1024));
    }
    return buffer;
}
