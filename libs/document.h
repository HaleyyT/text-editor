#ifndef DOCUMENT_H

#define DOCUMENT_H
/**
 * This file is the header file for all the document functions. You will be tested on the functions inside markdown.h
 * You are allowed to and encouraged multiple helper functions and data structures, and make your code as modular as possible. 
 * Ensure you DO NOT change the name of document struct.
 */

typedef struct{
    char *text;
    struct chunk *next;
} chunk;

typedef struct {
    chunk *head;
    uint64_t version;
} document;

// Functions from here onwards.
#endif
