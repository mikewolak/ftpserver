// include/daemon.h
#ifndef DAEMON_H
#define DAEMON_H

#include "config.h"

// Function to daemonize the process
int daemonize(void);

// Global flag for daemon mode
extern int daemon_mode;

#endif // DAEMON_H
