// src/daemon.c
#include "../include/config.h"
#include "../include/logging.h"

// Global flag for daemon mode
int daemon_mode = 0;

// Function to daemonize the process
int daemonize(void) {
    pid_t pid, sid;
    
    // Fork and exit the parent process
    pid = fork();
    if (pid < 0) {
        log_message(FTPLOG_ERROR, "Failed to fork daemon process: %s", strerror(errno));
        return 0;
    }
    
    // If we got a good PID, then we can exit the parent process
    if (pid > 0) {
        // Exit the parent process
        exit(EXIT_SUCCESS);
    }
    
    // Change the file mode mask
    umask(0);
    
    // Create a new SID for the child process
    sid = setsid();
    if (sid < 0) {
        log_message(FTPLOG_ERROR, "Failed to create new session: %s", strerror(errno));
        return 0;
    }
    
    // Change the current working directory to root
    // This prevents the current directory from being locked
    if (chdir("/") < 0) {
        log_message(FTPLOG_ERROR, "Failed to change directory: %s", strerror(errno));
        return 0;
    }
    
    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    // Redirect standard file descriptors to /dev/null
    int null_fd = open("/dev/null", O_RDWR);
    if (null_fd < 0) {
        return 0;
    }
    
    // Duplicate to standard input, output, and error
    if (dup2(null_fd, STDIN_FILENO) < 0 ||
        dup2(null_fd, STDOUT_FILENO) < 0 ||
        dup2(null_fd, STDERR_FILENO) < 0) {
        close(null_fd);
        return 0;
    }
    
    close(null_fd);
    
    return 1;
}
