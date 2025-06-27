// include/commands.h
#ifndef COMMANDS_H
#define COMMANDS_H

#include "config.h"
#include "client.h"

// Process an FTP command
void process_command(client_t *client, const char *command, const char *arg);

// Send response to client
void send_response(int socket, int code, const char *message);

#endif // COMMANDS_H
