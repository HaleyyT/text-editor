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

    // 1) Create a unique FIFO name for this client using PID
    char client_fifo[128];
    snprintf(client_fifo, sizeof(client_fifo), "client_fifo_%d", getpid());
    // Attempt to create FIFO for client
    if (mkfifo(client_fifo, 0666) == -1) {
        perror("mkfifo client");
        return 1;
    }

    //Prepare request structure
    edit_request req = {0};
    strcpy(req.client_fifo, client_fifo);
    strncpy(req.command, argv[1], sizeof(req.command) - 1);

    // -- ACCEPT EDITING COMMANDS --
    // If the command is "insert", expect two additional args: position and text
    if (strcmp(req.command, "insert") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s insert <pos> <text>\n", argv[0]);
            unlink(client_fifo);
            return 1;
        }
        req.pos = (size_t)atoi(argv[2]);
        strncpy(req.text, argv[3], sizeof(req.text) - 1);
    } 

    if (strcmp(req.command, "delete") == 0) {
        if (argc < 4) {
        fprintf(stderr, "Usage: %s delete <pos> <len>\n", argv[0]);
        unlink(client_fifo);
        return 1;
    }
    req.pos = (size_t)atoi(argv[2]);
    req.len = (size_t)atoi(argv[3]);
    }

    if (strcmp(req.command, "bold") == 0) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s bold <start> <end>\n", argv[0]);
        unlink(client_fifo);
        return 1;
    }
    req.pos = (size_t)atoi(argv[2]);       // start
    req.len = (size_t)atoi(argv[3]);       // end (store in len)
}

    // 3) Send request to server
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

    close(server_fd); //finish sending request 

    // 4) Read server response
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

    // otherwise, print response as EDIT lines
    char *line = strtok(buf, "\n");
    while (line) {
        printf("EDIT %s\n", line);
        line = strtok(NULL, "\n");
    }

    return 0;
}