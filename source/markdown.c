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



//Helper function: create deep copy of chunks
//CALLER FREE MEMO
chunk *deep_copy_chunks(chunk *head) {
    if (!head) {
        // Create a dummy empty chunk
        chunk *empty = malloc(sizeof(chunk));
        if (!empty) return NULL;
        empty->text = strdup_safe("");
        if (!empty->text) {
            free(empty);
            return NULL;
        }
        empty->next = NULL;
        return empty;
    }

    chunk *new_head = malloc(sizeof(chunk));
    if (!new_head) return NULL;

    new_head->text = strdup_safe(head->text);
    if (!new_head->text) {
        free(new_head);
        return NULL;
    }

    new_head->next = deep_copy_chunks(head->next);
    return new_head;
}




int markdown_delete(document *doc, uint64_t version, size_t pos, size_t len) {
    if (!doc || doc->version != version || len == 0) return -1;

    //Make a staged copy if not exists
    if (!doc->staged_head) {
        doc->staged_head = deep_copy_chunks(doc->head);
        if (!doc->staged_head) return -1;
    }

    chunk *curr = doc->staged_head;
    chunk *prev = NULL;
    size_t chars_seen = 0;
    size_t to_delete = len;

    while (curr && to_delete > 0) {
        if (!curr->text) {
            chunk *next = curr->next;
            if (prev){
                prev->next = next;
            } else {
                doc->staged_head = next;
            }
            free(curr);
            curr = next;
            continue;
        }

        size_t curr_len = strlen(curr->text);
        chars_seen += curr_len;  // Move this up before any potential free

        // Check if delete starts in this chunk
        if (chars_seen > pos) {
            size_t start = (pos > chars_seen - curr_len) ? pos - (chars_seen - curr_len) : 0;
            size_t available = curr_len - start;
            size_t del_len = (available >= to_delete) ? to_delete : available;

            memmove(curr->text + start, curr->text + start + del_len, curr_len - start - del_len + 1);
            to_delete -= del_len;
        }

        // Remove chunk if now empty
        if (strlen(curr->text) == 0) {
            chunk *next = curr->next;
            free(curr->text);
            free(curr);
            if (prev) {
                prev->next = next;
            } else {
                doc->staged_head = next;
            }
            curr = next;
        } else {
            prev = curr;
            curr = curr->next;
        }
    }
    return 0;
}



//Helper function to retrieve a string presentation of doc in its staged version
char *flatten_staged(const document *doc) {
    if (!doc || !doc->staged_head) return strdup_safe("");

    size_t total_len = 0;
    for (chunk *curr = doc->staged_head; curr != NULL; curr = curr->next) {
        total_len += strlen(curr->text);
    }

    char *res = malloc(total_len + 1);
    if (!res) return NULL;
    res[0] = '\0';

    for (chunk *curr = doc->staged_head; curr != NULL; curr = curr->next) {
        strcat(res, curr->text);
    }

    return res;
}


int apply_flat_insert(document *doc, size_t pos, const char *content) {
    char *flat = flatten_staged(doc);
    if (!flat) return -1;

    size_t len = strlen(flat);
    size_t new_len = len + strlen(content);
    char *new_doc = malloc(new_len + 1);
    if (!new_doc) {
        free(flat);
        return -1;
    }

    memcpy(new_doc, flat, pos);
    memcpy(new_doc + pos, content, strlen(content));
    memcpy(new_doc + pos + strlen(content), flat + pos, len - pos + 1);

    free(flat);

    // rebuild chunk list
    chunk *new_chunks = malloc(sizeof(chunk));
    if (!new_chunks) {
        free(new_doc);
        return -1;
    }
    new_chunks->text = new_doc;
    new_chunks->next = NULL;

    // free old staged
    chunk *old = doc->staged_head;
    while (old) {
        chunk *next = old->next;
        free(old->text);
        free(old);
        old = next;
    }
    doc->staged_head = new_chunks;
    return 0;
}



// === Edit Commands ===
int markdown_insert(document *doc, uint64_t version, size_t pos, const char *content) {
    if (!doc || !content) return -1;

    // Ensure staged version is ready
    if (!doc->staged_head) {
        doc->staged_head = deep_copy_chunks(doc->head);
        if (!doc->staged_head) {
            //printf("%s", "markdown_insert: Fail to deep copy head");
            return -1;
        }
    }

    return apply_flat_insert(doc, pos, content);

    char *debug_before = flatten_staged(doc);
    printf("[DEBUG before insert] staged = %s\n", debug_before);
    free(debug_before);

    printf("[DEBUG insert] Inserting \"%s\" at pos %zu for version %llu\n", content, pos, version);

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

        char* debug = flatten_staged(doc);
        printf("[DEBUG markdown_insert] after insert at pos %zu: %s\n", pos, debug);
        free(debug);
        return SUCCESS;
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

            char* debug = flatten_staged(doc);
            printf("[DEBUG markdown_insert] after insert at pos %zu: %s\n", pos, debug);
            free(debug);
            return 0;
        }
        chars_seen += curr_len;
        curr = curr->next;
    }

    //traverse through chunks of text and set its tail ptr to our inserted chunk
        // Fallback: if pos == total length, append to end
    size_t total_len = 0;
    chunk *temp = doc->staged_head;
    while (temp) {
        total_len += strlen(temp->text);
        temp = temp->next;
    }

    if (pos == total_len) {
        chunk *tail = doc->staged_head;
        if (!tail) {
            doc->staged_head = inserted_chunk;
        } else {
            while (tail->next) {
                tail = tail->next;
            }
            tail->next = inserted_chunk;
        }

        char* debug = flatten_staged(doc);
        printf("[DEBUG markdown_insert] after insert at pos %zu: %s\n", pos, debug);
        free(debug);
        return SUCCESS;
    }

    // Invalid position (past end of document)
    free(inserted_chunk->text);
    free(inserted_chunk);
    return -1;

}


// === Formatting Commands ===
int markdown_newline(document *doc, size_t version, size_t pos) {
    if (!doc || doc->version != version) return -1;

    if (!doc->staged_head){
        doc->staged_head = deep_copy_chunks(doc->head);
        if (!doc->staged_head) return -1;
    }
    //check valid position within the total length to insert newline 
    size_t total_len = 0;
    for (chunk* curr = doc->staged_head; curr; curr = curr->next){
        total_len += strlen(curr->text);
    }
    if (pos > total_len) return -1;

    return markdown_insert(doc, version, pos, "\n");
}


int markdown_heading(document *doc, uint64_t version, size_t level, size_t pos) {
    if (!doc || doc->version != version || level < 1 || level > 6) return -1;

    if (!doc->staged_head){
        doc->staged_head = deep_copy_chunks(doc->head);
        if (!doc->staged_head) return -1;
    }

    char prefix[8]; // max: 6 "#" + 1 space + 1 null terminator 
    memset(prefix, '#', level);
    prefix[level] = ' '; //add space after the '#' level 
    prefix[level + 1] = '\0'; //add null terminator after space to end the string 

    if (markdown_insert(doc, version, pos, prefix) != 0) return -1;
    return SUCCESS;
}

int markdown_bold(document *doc, uint64_t version, size_t start, size_t end) {
    if (!doc || doc->version != version || start >= end) return -1;

    if (!doc->staged_head) {
        doc->staged_head = deep_copy_chunks(doc->head);
        if (!doc->staged_head) return -1;
    }

    if (markdown_insert(doc, version, end, "**") != 0) {
        printf("%s", "make bold text fail");
        return -1;
    }
    if (markdown_insert(doc, version, start, "**") != 0) {
        printf("%s", "make bold text fail");
        return -1;
    }

    return SUCCESS;
}


int markdown_italic(document *doc, uint64_t version, size_t start, size_t end) {
    if (!doc || doc->version != version || start >= end) return -1;

    if (!doc->staged_head){
        doc->staged_head = deep_copy_chunks(doc->head);
        if (!doc->staged_head) {
            printf("%s", "ITALIC: unable to create deep copy of head");
            return -1;
        }
    }
    // Insert "*" at the end first to avoid shifting start position
    if (markdown_insert(doc, version, end, "*") != 0) {
        printf("%s", "ITALIC: unable to italicise");
        return -1; 
    }
    if (markdown_insert(doc, version, start, "*") != 0) {
        printf("%s", "ITALIC: unable to italicise");
        return -1; 
    }
    return SUCCESS;
}

int markdown_blockquote(document *doc, uint64_t version, size_t pos) {
    (void)doc; (void)version; (void)pos;
    return SUCCESS;
}


int markdown_ordered_list(document *doc, uint64_t version, size_t pos) {
    if (!doc || doc->version != version) return -1;

    if (!doc->staged_head) {
        doc->staged_head = deep_copy_chunks(doc->head);
        if (!doc->staged_head) return -1;
    }

    size_t offset = pos;
    int index = 1;

    while (1) {
        char prefix[16];
        snprintf(prefix, sizeof(prefix), "%d. ", index);  // e.g., "1. ", "2. "

        if (markdown_insert(doc, version, offset, prefix) != 0) return -1;

        offset += strlen(prefix);

        // Search for the next newline
        chunk *curr = doc->staged_head;
        size_t chars_seen = 0;
        int found = 0;

        while (curr) {
            size_t len = strlen(curr->text);
            for (size_t i = 0; i < len; ++i) {
                if (chars_seen + i >= offset && curr->text[i] == '\n') {
                    offset = chars_seen + i + 1;  // after \n
                    found = 1;
                    break;
                }
            }
            if (found) break;
            chars_seen += len;
            curr = curr->next;
        }

        if (!found) break;
        index++;
    }

    return 0;
}




int markdown_unordered_list(document *doc, uint64_t version, size_t pos) {
    if (!doc || doc->version != version) return -1;

    if (!doc->staged_head) {
        doc->staged_head = deep_copy_chunks(doc->head);
        if (!doc->staged_head) return -1;
    }

    char *str_flat = flatten_staged(doc);
    if (!str_flat) return -1;

    size_t len = strlen(str_flat);
    size_t shift = 0;

    printf("staged content before list formatting: \n%s\n", str_flat);

    for (size_t i = pos; i < len;) {
        printf("Inserting \"- \" at position: %zu\n", i + shift);
        if (markdown_insert(doc, version, i + shift, "- ") != 0){
            printf("fail to insert - sign at position: %zu\n", i+shift);
            free(str_flat);
            return -1;
        }
        shift += 2;

        //move to the next new line 
        while (i < len && str_flat[i] != '\n') i++;
        i++; //pass '\n'
    }
    char *check = flatten_staged(doc);
    printf("Staged content after list formatting:\n%s\n", check);
    free(check);

    free(str_flat);
    return SUCCESS;
}


int markdown_code(document *doc, uint64_t version, size_t start, size_t end) {
    if (!doc || doc->version != version || start >= end) {
        printf("%s"," Markdown_code: conditions not met \n");
        return -1;
    }

    if (!doc->staged_head) {
        doc->staged_head = deep_copy_chunks(doc->head);
        if (!doc->staged_head) return -1;
    }

    // Insert opening backtick first
    if (markdown_insert(doc, version, end, "`") != 0) {
        printf("%s"," Markdown_code: fail inserting ' at end \n");
        return -1;
    }
    if (markdown_insert(doc, version, start, "`") != 0) {
        printf("%s"," Markdown_code: fail inserting ' at start \n");
        return -1;
    }
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

    char *res = malloc(total_len + 1);
    if (!res) return NULL;
    res[0] = '\0';

    for (chunk *curr = doc->head; curr != NULL; curr = curr->next){
        strcat(res, curr->text);
    }
    return res;
}


// === Versioning ===
void markdown_increment_version(document *doc) {
    if (!doc || !doc->staged_head) {
        printf("INC_VERSION: Nothing to commit\n");
        return;
    }

    printf("INC_VERSION: Committing staged to head\n");

    // Free the old committed content
    chunk *old = doc->head;
    while (old) {
        chunk *next = old->next;
        free(old->text);
        free(old);
        old = next;
    }
    // Replace head with staged version
    doc->head = doc->staged_head;
    doc->staged_head = NULL;
    doc->version++;
}

#ifdef DEBUG_MARKDOWN
//gcc -DDEBUG_MARKDOWN markdown.c -o markdown

//gcc -DDEBUG_MARKDOWN -g -fsanitize=address -o markdown markdown.c
//leaks --atExit -- ./markdown

int main() {
    document *doc = markdown_init();

    // v0: Insert original content
    markdown_insert(doc, 0, 0, "Hello, World.");
    markdown_increment_version(doc);  // → v1

    // v1: delete everything
    markdown_delete(doc, 1, 0, strlen("Hello, World."));

    // insert Foo at pos 0, then Bar at pos 3
    markdown_insert(doc, 1, 0, "Foo");
    markdown_insert(doc, 1, 3, "Bar");

    markdown_increment_version(doc);    // → v2

    // Check result
    char* str = markdown_flatten(doc);
    printf("Result: \"%s\"\n", str);  // Should print FooBar
    free(str);
    markdown_free(doc);
    return 0;

    /*
    document *doc = markdown_init();

    // Version 0: Insert list text
    markdown_insert(doc, 0, 0, "List item\n");
    markdown_increment_version(doc); // Version 1

    // Version 1: Convert to unordered list
    markdown_unordered_list(doc, 1, 0);
    markdown_increment_version(doc); // Version 2

    // Version 2: Add newline after list
    markdown_newline(doc, 2, strlen("- List item"));
    markdown_increment_version(doc); // Version 3

    // v3: Insert code snippet
    char *staged3 = flatten_staged(doc);
    size_t insert_pos = strlen(staged3);
    free(staged3);
    markdown_insert(doc, 3, insert_pos, "x = 5;");
    markdown_increment_version(doc); // version 4

    //compute range BEFORE incrementing again
    char *staged4 = flatten_staged(doc);
    size_t start = strlen("- List item\n\n");
    size_t end = strlen(staged4);  // end of "x = 5;"
    printf("DEBUG code wrap range: start=%zu, end=%zu\n", start, end);
    free(staged4);

    // v4: Add backticks
    markdown_code(doc, 4, start, end);
    markdown_increment_version(doc); // version 5


    // Final: Flatten and print
    char *result = markdown_flatten(doc);
    printf("Final Output:\n%s\n", result);  // Expect:
    // - List item
    // `x = 5;`
    free(result);
    markdown_free(doc);
    return 0;
    */

}
#endif


