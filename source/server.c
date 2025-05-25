// TODO: server code that manages the document and handles client instructions
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include "../libs/ipc.h"
#include "markdown.h" // for the document functions

#define SERVER_FIFO "markdown_server"

int main(int argc, char *argv[]) {
    unlink(SERVER_FIFO);
    (void)argc; (void)argv;

    printf("Server starting...\n");

    // Create server FIFO if it doesn't exist
    if (mkfifo(SERVER_FIFO, 0666) == -1) {
        perror("mkfifo");
        // May already exist, so continue
    }

    int server_fd = open(SERVER_FIFO, O_RDONLY);
    if (server_fd < 0) {
        perror("open server fifo");
        return 1;
    }

    document *doc = markdown_init();

    while (1) {
        edit_request req;
        ssize_t bytes = read(server_fd, &req, sizeof(req));
        if (bytes <= 0) continue;

        if (strcmp(req.command, "DISCONNECT") == 0) {
        // Clean up client FIFO or perform acknowledgment
        //FIFO stil open and respond
        return 0;

        int client_fd = open(req.client_fifo, O_WRONLY);
        if (client_fd >= 0) {
            // You can just send an empty string or "DISCONNECTED"
            char *flat = markdown_flatten(doc);
            char response[4096];
            snprintf(response, sizeof(response),
                    "role:editor\nversion:%llu\nlength:%zu\n%s",
                    doc->version, strlen(flat), flat);
            write(client_fd, response, strlen(response) + 1);
            free(flat);
            close(client_fd);
        }

        printf("[SERVER] Client %s disconnected\n", req.client_fifo);
        continue; // or retur22n, if you're exiting the thread
    }


        printf("Received request: %s at %zu: %s\n", req.command, req.pos, req.text);

        // Example: insert
        if (strcmp(req.command, "insert") == 0) {
            markdown_insert(doc, doc->version, req.pos, req.text);
            markdown_increment_version(doc);
        }

        // Send response to client
        int client_fd = open(req.client_fifo, O_WRONLY);
        if (client_fd < 0) {
            perror("open client fifo");
            continue;
        }

        char *flat = markdown_flatten(doc);
        char response[4096];
        snprintf(response, sizeof(response),
                "role:editor\nversion:%llu\nlength:%zu\n%s",
                doc->version, strlen(flat), flat);
        write(client_fd, response, strlen(response) + 1);
        close(client_fd);
        free(flat);
    }

    markdown_free(doc);
    unlink(SERVER_FIFO);
    return 0;
}

