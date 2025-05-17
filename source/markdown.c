#include "../libs/markdown.h"
#include <stdlib.h>
#include <string.h>



#define SUCCESS 0 

// === Init and Free ===
document *markdown_init(void) {
    document* new_doc = malloc(sizeof(document));
    if (new_doc == NULL) {
        return NULL;
    }
    new_doc->head = NULL;
    new_doc->staged_head = NULL;
    new_doc->version = 0;
    return new_doc;
}

void markdown_free(document *doc) {
    if (!doc) return;

    //create a chunk ptr - curr 
    chunk *curr = doc->head;
    while (curr){
        chunk *next = curr->next;
        free(curr->text);
        free(curr);
        curr = next;
    }

    //Use the pointer to traverse through staged_head and free it as well
    curr = doc->staged_head;
    while (curr) {
        chunk *next = curr->next;
        free(curr->text);
        free(curr);
        curr = next;
    }

    free(doc);
}

char *strdup_safe(const char *s) {
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    if (copy) {
        strcpy(copy, s);
    }
    return copy;
}

char *strndup_safe(const char *s, size_t n) {
    char *copy = malloc(n + 1);
    if (copy) {
        strncpy(copy, s, n);
        copy[n] = '\0';
    }
    return copy;
}


// === Edit Commands ===
int markdown_insert(document *doc, uint64_t version, size_t pos, const char *content) {
    if (!doc || !content) return -1;

    //link the ptr of the specific version document to the text chunk
    if (doc->version != version){
        return -1;
    }

    //If there is no next text, the ptr to next text chunk to NULL
    chunk *inserted_chunk = malloc(sizeof(chunk));
    if (!inserted_chunk) return -1;

    inserted_chunk->text = strdup_safe(content);
    if (!inserted_chunk->text){
        free(inserted_chunk);
        return -1;
    }
    inserted_chunk->next = NULL;

    //Insert at beginning 
    if (pos == 0){
        inserted_chunk->next = doc->staged_head;
        doc->staged_head = inserted_chunk;
        return 0;
    }

    //If there is next text, link the ptr of next text to previous doc->next
    chunk* curr = doc->staged_head;
    size_t chars_seen = 0;

    while (curr){
        size_t curr_len = strlen(curr->text);
        if (chars_seen + curr_len >= pos) {
            //split if insert in the middle of the chunk
            size_t offset = pos - chars_seen;

            char *before = strndup_safe(curr->text, offset);
            char *after = strdup_safe(curr->text + offset);

            free(curr->text);
            curr->text = before;

            chunk *after_chunk = malloc(sizeof(chunk));
            if (!after_chunk) {
                free(inserted_chunk->text);
                free(inserted_chunk);
                return -1;
            }
            after_chunk->text = after;
            after_chunk->next = curr->next;

            inserted_chunk->next = after_chunk;
            curr->next = inserted_chunk;
            return 0;
        }
        chars_seen += curr_len;
        curr = curr->next;
    }

    //traverse through chunks of text and set its tail ptr to our inserted chunk
    struct chunk *tail = doc->staged_head;
    while (tail && tail->next){
        tail = tail->next;
    }

    if (tail){
        tail->next = inserted_chunk;
    } else{
        doc->staged_head = inserted_chunk;
    }

    return SUCCESS;
}

int markdown_delete(document *doc, uint64_t version, size_t pos, size_t len) {
    (void)doc; (void)version; (void)pos; (void)len;
    return SUCCESS;
}

// === Formatting Commands ===
int markdown_newline(document *doc, size_t version, size_t pos) {
    (void)doc; (void)version; (void)pos;
    return SUCCESS;
}

int markdown_heading(document *doc, uint64_t version, size_t level, size_t pos) {
    (void)doc; (void)version; (void)level; (void)pos;
    return SUCCESS;
}

int markdown_bold(document *doc, uint64_t version, size_t start, size_t end) {
    (void)doc; (void)version; (void)start; (void)end;
    return SUCCESS;
}

int markdown_italic(document *doc, uint64_t version, size_t start, size_t end) {
    (void)doc; (void)version; (void)start; (void)end;
    return SUCCESS;
}

int markdown_blockquote(document *doc, uint64_t version, size_t pos) {
    (void)doc; (void)version; (void)pos;
    return SUCCESS;
}

int markdown_ordered_list(document *doc, uint64_t version, size_t pos) {
    (void)doc; (void)version; (void)pos;
    return SUCCESS;
}

int markdown_unordered_list(document *doc, uint64_t version, size_t pos) {
    (void)doc; (void)version; (void)pos;
    return SUCCESS;
}

int markdown_code(document *doc, uint64_t version, size_t start, size_t end) {
    (void)doc; (void)version; (void)start; (void)end;
    return SUCCESS;
}

int markdown_horizontal_rule(document *doc, uint64_t version, size_t pos) {
    (void)doc; (void)version; (void)pos;
    return SUCCESS;
}

int markdown_link(document *doc, uint64_t version, size_t start, size_t end, const char *url) {
    (void)doc; (void)version; (void)start; (void)end; (void)url;
    return SUCCESS;
}

// === Utilities ===
void markdown_print(const document *doc, FILE *stream) {
    (void)doc; (void)stream;
}

char *markdown_flatten(const document *doc) {
    if (!doc || !doc->head) return strdup_safe("");

    size_t total_len = 0;
    for (chunk *curr = doc->head; curr != NULL; curr = curr->next){
        total_len += strlen(curr->text);
    }

    //If result is empty, add terminating sign 
    char *res = malloc(total_len + 1);
    if (!res) return NULL;
    res[0] = '\0';

    //keep traversing through all the current chunk's text and append to res 
    for (chunk *curr = doc->head; curr != NULL; curr = curr->next){
        strcat(res, curr->text);
    }
    //return res which is 1 string containing all chunks 
    return res;
}

// === Versioning ===
void markdown_increment_version(document *doc) {
    if (!doc || !doc->staged_head) return;

    chunk *tail = doc->staged_head;

    // Check if there's only 1 staged chunk
    if (!tail->next) {
        tail->next = doc->head;
        doc->head = doc->staged_head;
        doc->staged_head = NULL;
        doc->version++;
        return;
    }

    // Traverse to last chunk in staged
    while (tail->next) {
        tail = tail->next;
    }

    tail->next = doc->head;
    doc->head = doc->staged_head;
    doc->staged_head = NULL;
    doc->version++;
}



