#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include "../libs/ipc.h"
#include "markdown.h" 

#define SERVER_FIFO "markdown_server"

/**
 * Main server loop to accept edit requests from clients via FIFO.
 * Manages a shared markdown document state and returns the result to clients.
 */
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // Always unlink server FIFO on startup to avoid stale state
    unlink(SERVER_FIFO);

    printf("Server starting...\n");
    printf("Server PID: %d\n", getpid());
    fflush(stdout); 

    // Create named pipe if it doesn't exist
    if (mkfifo(SERVER_FIFO, 0666) == -1) {
        perror("mkfifo"); // Might already exist, continue running
    }

    // Open FIFO for reading edit requests
    int server_fd = open(SERVER_FIFO, O_RDONLY);
    if (server_fd < 0) {
        perror("open server fifo");
        return 1;
    }

    // Initialise shared document
    document *doc = markdown_init();

    while (1) {
        struct edit_request req;
        ssize_t bytes = read(server_fd, &req, sizeof(req));
        if (bytes <= 0) continue; // Retry on empty or failed read

        //handle DISCONNECT request
        if (strcmp(req.command, "DISCONNECT") == 0) {
            // Open client FIFO for writing reply
            int client_fd = open(req.client_fifo, O_WRONLY);
            if (client_fd >= 0) {
                char *flat = markdown_flatten(doc);

                // Format response to match expected test format
                char response[4096];
                snprintf(response, sizeof(response),
                         "role:editor\nversion:%llu\nlength:%zu\n%s",
                         (unsigned long long)doc->version, strlen(flat), flat);

                write(client_fd, response, strlen(response) + 1);
                free(flat);
                close(client_fd);
            }

            // Print disconnect event for debug
            printf("[SERVER] Client %s disconnected\n", req.client_fifo);
            continue; // Stay alive for other clients
        }

        // Log received request
        printf("Received request: %s at %zu: %s\n", req.command, req.pos, req.text);

        // Process editing operations
        if (strcmp(req.command, "insert") == 0) {
            markdown_insert(doc, doc->version, req.pos, req.text);
            markdown_increment_version(doc); // Commit change to document
        } else if (strcmp(req.command, "delete") == 0) {
            markdown_delete(doc, doc->version, req.pos, req.len);
            markdown_increment_version(doc);
        }
        else if (strcmp(req.command, "bold") == 0) {
            // if your API is (doc, version, start, end):
            markdown_bold(doc, doc->version, req.pos, req.len);
            markdown_increment_version(doc);
        }

        // Reply with updated document state
        int client_fd = open(req.client_fifo, O_WRONLY);
        if (client_fd < 0) {
            perror("open client fifo");
            continue;
        }

        char *flat = markdown_flatten(doc);
        size_t flat_len = strlen(flat);
        if (flat_len > 0 && flat[flat_len - 1] == '\n') {
            printf("[DEBUG] flat already ends with newline\n");
        } else {
            printf("[DEBUG] flat does NOT end with newline\n");
}
        char response[4096];
        snprintf(response, sizeof(response),
                 "role:editor\nversion:%llu\nlength:%zu\n%s",
                 (unsigned long long)doc->version, strlen(flat), flat);

        write(client_fd, response, strlen(response) + 1);
        close(client_fd);
        free(flat);
    }
    
    // Cleanup on shutdown
    markdown_free(doc);
    unlink(SERVER_FIFO);
    return 0;
}