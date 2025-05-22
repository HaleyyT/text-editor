#include "../libs/markdown.h"
#include <stdlib.h>
#include <string.h>

static char *shared_flat = NULL;
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

    // On first modification, prepare shared_flat
    if (!shared_flat) {
        char *base = markdown_flatten(doc);
        if (!base) return -1;
        shared_flat = strdup_safe(base);
        free(base);
        if (!shared_flat) return -1;
    }

    size_t total_len = strlen(shared_flat);
    if (pos >= total_len) return -1;

    size_t actual_len = (pos + len > total_len) ? total_len - pos : len;
    size_t new_len = total_len - actual_len;

    char *new_flat = malloc(new_len + 1);
    if (!new_flat) return -1;

    memcpy(new_flat, shared_flat, pos);                         // copy before delete range
    memcpy(new_flat + pos, shared_flat + pos + actual_len, total_len - pos - actual_len); // after delete
    new_flat[new_len] = '\0';

    free(shared_flat);
    shared_flat = new_flat;

    // Rebuild staged_head
    chunk *new_chunk = malloc(sizeof(chunk));
    if (!new_chunk) return -1;
    new_chunk->text = strdup_safe(shared_flat);
    if (!new_chunk->text) {
        free(new_chunk);
        return -1;
    }
    new_chunk->next = NULL;

    // Free old staged_head
    chunk *curr = doc->staged_head;
    while (curr) {
        chunk *next = curr->next;
        free(curr->text);
        free(curr);
        curr = next;
    }

    doc->staged_head = new_chunk;
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
    if (!doc || !content) return -1;

    // Initialize shared_flat if not already done
    if (!shared_flat) {
        char *base = markdown_flatten(doc);
        if (!base) return -1;
        shared_flat = strdup_safe(base);
        free(base);
        if (!shared_flat) return -1;
    }

    size_t len = strlen(shared_flat);
    size_t insert_len = strlen(content);

    if (pos > len) return -1;

    size_t new_len = len + insert_len;
    char *new_doc = malloc(new_len + 1);
    if (!new_doc) return -1;

    // Apply insertion
    memcpy(new_doc, shared_flat, pos);
    memcpy(new_doc + pos, content, insert_len);
    memcpy(new_doc + pos + insert_len, shared_flat + pos, len - pos);
    new_doc[new_len] = '\0';

    // Update shared_flat
    free(shared_flat);
    shared_flat = new_doc;

    // Rebuild staged_head from updated flat string
    chunk *new_chunk = malloc(sizeof(chunk));
    if (!new_chunk) return -1;
    new_chunk->text = strdup_safe(shared_flat);
    if (!new_chunk->text) {
        free(new_chunk);
        return -1;
    }
    new_chunk->next = NULL;

    // Free old staged_head
    chunk *curr = doc->staged_head;
    while (curr) {
        chunk *next = curr->next;
        free(curr->text);
        free(curr);
        curr = next;
    }

    doc->staged_head = new_chunk;
    return 0;
}





// === Edit Commands ===
int markdown_insert(document *doc, uint64_t version, size_t pos, const char *content) {
    if (!doc || !content || doc->version != version) return -1;

    // Ensure staged version is ready
    if (!doc->staged_head) {
        doc->staged_head = deep_copy_chunks(doc->head);
        if (!doc->staged_head) return -1;
    }

    return apply_flat_insert(doc, pos, content);
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

    if (shared_flat) {
        free(shared_flat);
        shared_flat = NULL;
    }
}

#ifdef DEBUG_MARKDOWN
//gcc -DDEBUG_MARKDOWN markdown.c -o markdown

//gcc -DDEBUG_MARKDOWN -g -fsanitize=address -o markdown markdown.c
//leaks --atExit -- ./markdown

int main() {
    document *doc = markdown_init();

    // v0: Insert original content
    markdown_insert(doc, 0, 0, "Hello, World.");
    markdown_increment_version(doc);  // now v1

    // v1: delete everything
    markdown_delete(doc, 1, 0, strlen("Hello, World."));

    // insert Foo at pos 0
    markdown_insert(doc, 1, 0, "Foo");

    // insert Bar at pos 3 (after Foo)
    markdown_insert(doc, 1, 3, "Bar");

    markdown_increment_version(doc);  // now v2

    char* result = markdown_flatten(doc);
    printf("Result: \"%s\"\n", result);  // Expect: FooBar
    free(result);
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


