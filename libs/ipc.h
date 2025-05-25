#ifndef IPC_H
#define IPC_H

#include <stddef.h>

typedef struct {
    char client_fifo[128];   // Path of the client's FIFO
    char command[16];        // e.g., "insert", "bold", etc.
    size_t pos;
    size_t len;              // for delete or formatting range
    char text[1024];         // for insert, optional
} edit_request;

#endif
