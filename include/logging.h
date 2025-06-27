// include/logging.h
#ifndef LOGGING_H
#define LOGGING_H

#include "config.h"

// Log levels - renamed to avoid conflict with syslog.h
typedef enum {
    FTPLOG_INFO,      // Renamed from FTPLOG_INFO
    FTPLOG_ERROR,     // Renamed from FTPLOG_ERROR
    FTPLOG_DEBUG,     // Renamed from FTPLOG_DEBUG
    FTPLOG_TRANSFER   // Renamed from FTPLOG_TRANSFER
} log_level_t;

// Initialize logging
void log_init(void);

// Initialize file logging
int log_init_file(const char *program_name);

// Close logging
void log_close(void);

// Log a message
void log_message(log_level_t level, const char *format, ...);

// Format transfer rate (bytes/sec to MB/sec)
char* format_transfer_rate(double bytes_per_sec, char* buffer, size_t buffer_size);

#endif // LOGGING_H
