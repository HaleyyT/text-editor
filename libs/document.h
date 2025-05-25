#ifndef DOCUMENT_H
#define DOCUMENT_H
#include <stdint.h>

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



#endif