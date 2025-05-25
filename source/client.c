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

    char client_fifo[128];
    snprintf(client_fifo, sizeof(client_fifo), "client_fifo_%d", getpid());
    mkfifo(client_fifo, 0666);

    edit_request req = {0};
    strcpy(req.client_fifo, client_fifo);
    strcpy(req.command, argv[1]);

    if (strcmp(req.command, "insert") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s insert <pos> <text>\n", argv[0]);
            return 1;
        }
        req.pos = atoi(argv[2]);
        strncpy(req.text, argv[3], sizeof(req.text) - 1);
    }

    // Send request
    int server_fd = open(SERVER_FIFO, O_WRONLY);
    write(server_fd, &req, sizeof(req));
    close(server_fd);

    // Read response
    int client_fd = open(client_fifo, O_RDONLY);
    char buf[4096];
    ssize_t bytes = read(client_fd, buf, sizeof(buf));
    if (bytes > 0) {
        buf[bytes] = '\0';
        printf("%s", buf);
    }

    close(client_fd);
    unlink(client_fifo);
    return 0;
}
