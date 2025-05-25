#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "ipc.h"

#define SERVER_FIFO "markdown_server"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [args...]\n", argv[0]);
        return 1;
    }

    // Create a unique FIFO name for this client using PID
    char client_fifo[128];
    snprintf(client_fifo, sizeof(client_fifo), "client_fifo_%d", getpid());
    if (mkfifo(client_fifo, 0666) == -1) {
        perror("mkfifo client");
        return 1;
    }

    // Fill the request
    edit_request req = {0};
    strcpy(req.client_fifo, client_fifo);
    strncpy(req.command, argv[1], sizeof(req.command) - 1);

    if (strcmp(req.command, "insert") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s insert <pos> <text>\n", argv[0]);
            unlink(client_fifo);
            return 1;
        }
        req.pos = (size_t)atoi(argv[2]);
        strncpy(req.text, argv[3], sizeof(req.text) - 1);
    }

    // Send request to server
    int server_fd = open(SERVER_FIFO, O_WRONLY);
    if (server_fd < 0) {
        perror("open server FIFO");
        unlink(client_fifo);
        return 1;
    }

    if (write(server_fd, &req, sizeof(req)) != sizeof(req)) {
        perror("write request");
        close(server_fd);
        unlink(client_fifo);
        return 1;
    }

    close(server_fd);

    // Read server response
    int client_fd = open(client_fifo, O_RDONLY);
    if (client_fd < 0) {
        perror("open client FIFO");
        unlink(client_fifo);
        return 1;
    }

    char buf[4096];
    ssize_t bytes = read(client_fd, buf, sizeof(buf) - 1);
    close(client_fd);
    unlink(client_fifo);

    if (bytes <= 0) return 0;

    buf[bytes] = '\0';

    // If DISCONNECT, just exit silently
    if (strcmp(req.command, "DISCONNECT") == 0) {
        return 0;
    }

    // Otherwise, print response as EDIT lines
    char *saveptr;
    char *line = strtok(buf, "\n", &saveptr);
    while (line) {
        printf("EDIT %s\n", line);
        line = strtok(NULL, "\n", &saveptr);
    }

    return 0;
}
