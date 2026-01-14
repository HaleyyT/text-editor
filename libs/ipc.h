#ifndef DOCUMENT_H
#define DOCUMENT_H
#include <stdint.h>
#include <stddef.h>   // for size_t

#ifndef COMMAND_MAX
#define COMMAND_MAX 32
#endif

#ifndef TEXT_MAX
#define TEXT_MAX 4096
#endif

#ifndef FIFO_MAX
#define FIFO_MAX 256
#endif


/**
 * This file is the header file for all the document functions. You will be tested on the functions inside markdown.h
 * You are allowed to and encouraged multiple helper functions and data structures, and make your code as modular as possible. 
 * Ensure you DO NOT change the name of document struct.
 */

//Chunk store text and has ptr to taverse to next 

typedef enum { EDIT_INSERT, EDIT_DELETE } edit_type;

typedef struct edit {
    edit_type type;
    size_t pos;
    size_t len;      // for delete
    char *text;      // for insert
    struct edit *next;
} edit;


typedef struct chunk {
    char *text;
    struct chunk *next;
} chunk;


typedef struct document {
    chunk *staged_head;
    chunk *head;
    uint64_t version;
} document;

typedef struct edit_request {
    char command[COMMAND_MAX];
    size_t pos;
    size_t len; 
    char text[TEXT_MAX];
    char client_fifo[FIFO_MAX];
} edit_request;


#endif