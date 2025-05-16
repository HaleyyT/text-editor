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
    new_doc->version = 0;
    return new_doc;
}

void markdown_free(document *doc) {
    if (!doc) return;
    free(doc);
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

    inserted_chunk->text = strdup(content);
    if (!inserted_chunk->text){
        free(inserted_chunk);
        return -1;
    }
    inserted_chunk->next = NULL;

    //Insert at beginning 
    if (pos == 0){
        inserted_chunk->next = doc->head;
        doc->head = inserted_chunk;
        return 0;
    }

    //If there is next text, link the ptr of next text to previous doc->next
    chunk* curr = doc->head;
    size_t chars_seen = 0;

    while (curr){
        size_t curr_len = strlen(curr->text);
        if (chars_seen + curr_len >= pos) {
            //split if insert in the middle of the chunk
            size_t offset = pos - chars_seen;

            char *before = strndup(curr->text, offset);
            char *after = strdup(curr->text + offset);

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
    chunk *tail = doc->head;
    while (tail && tail->next){
        tail = tail->next;
    }

    if (tail){
        tail->next = inserted_chunk;
    } else{
        doc->head = inserted_chunk;
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
    (void)doc;
    return strdup("");
}

// === Versioning ===
void markdown_increment_version(document *doc) {
    if (doc) doc->version++;
}



