#include "../libs/markdown.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef enum { EDIT_INSERT, EDIT_DELETE } edit_type;

typedef struct edit {
    edit_type type;
    size_t pos;
    size_t len;      // for delete
    char *text;      // for insert
    struct edit *next;
} edit;

static edit *edit_queue = NULL;

static char *shared_flat = NULL;
static char *base_flat = NULL; 
static uint64_t flat_version = (uint64_t)(-1); 
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



void ensure_shared_flat_initialized(document *doc) {
    if (flat_version == doc->version) {
        // already initialized for this version
        return;
    }
    // Clear previous state
    if (shared_flat) { free(shared_flat); shared_flat = NULL; }
    if (base_flat)   { free(base_flat);   base_flat = NULL; }

    base_flat = markdown_flatten(doc);  // snapshot of committed head
    if (!base_flat) return;

    shared_flat = strdup_safe(base_flat);
    if (!shared_flat) return;

    // Rebuild staged_head from shared_flat
        if (!doc->staged_head) {
        chunk *new_chunk = malloc(sizeof(chunk));
        if (!new_chunk) return;
        new_chunk->text = strdup_safe(shared_flat);
        if (!new_chunk->text) {
            free(new_chunk);
            return;
        }
        new_chunk->next = NULL;

        // Free old staged_head (ALWAYS)
        chunk *curr = doc->staged_head;
        while (curr) {
            chunk *next = curr->next;
            free(curr->text);
            free(curr);
            curr = next;
        }
        doc->staged_head = new_chunk;

    }
    flat_version = doc->version;  //update version tracker
    printf("[DEBUG ensure] base_flat = \"%s\"\n", base_flat);
    printf("[DEBUG ensure] shared_flat initialized = \"%s\"\n", shared_flat);
}


/*

int markdown_delete(document *doc, uint64_t version, size_t pos, size_t len) {
    if (!doc || doc->version != version || len == 0) return -1;

    ensure_shared_flat_initialized(doc);
    if (!shared_flat) return -1;

    size_t total_len = strlen(shared_flat);
    if (pos >= total_len) return -1;

    size_t actual_len = (pos + len > total_len) ? total_len - pos : len;
    size_t new_len = total_len - actual_len;

    char *new_flat = malloc(new_len + 1);
    if (!new_flat) return -1;

    memcpy(new_flat, shared_flat, pos);
    memcpy(new_flat + pos, shared_flat + pos + actual_len, total_len - pos - actual_len);
    new_flat[new_len] = '\0';

    free(shared_flat);
    shared_flat = new_flat;

    printf("[DEBUG markdown_delete] shared_flat after = \"%s\"\n", shared_flat);

    // Rebuild staged_head
    chunk *new_chunk = malloc(sizeof(chunk));
    if (!new_chunk) return -1;
    new_chunk->text = strdup_safe(shared_flat);
    if (!new_chunk->text) {
        free(new_chunk);
        return -1;
    }
    new_chunk->next = NULL;

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
*/


//Helper function to retrieve a string presentation of doc in its staged version
char *flatten_staged(const document *doc) {
    if (!doc || !doc->staged_head) return strdup_safe("");

    return markdown_flatten(doc);
}




int apply_flat_insert(document *doc, size_t pos, const char *content) {
    if (!doc || !shared_flat || !content) return -1;

    printf("[DEBUG apply_flat_insert] BEFORE insert: shared_flat = \"%s\"\n", shared_flat);
    printf("[DEBUG apply_flat_insert] inserting \"%s\" at pos %zu\n", content, pos);

    size_t len = strlen(shared_flat);
    size_t insert_len = strlen(content);

    if (pos > len) {
        pos = len;
        printf("%s", "[DEBUG apply_flat_insert] pos > len, FIX RANGE\n");
    }

    size_t new_len = len + insert_len;
    char *new_doc = malloc(new_len + 1);
    if (!new_doc) return -1;

    memcpy(new_doc, shared_flat, pos);
    memcpy(new_doc + pos, content, insert_len);
    memcpy(new_doc + pos + insert_len, shared_flat + pos, len - pos);
    new_doc[new_len] = '\0';

    free(shared_flat);
    shared_flat = new_doc;

    // Rebuild staged_head from updated shared_flat
    chunk *new_chunk = malloc(sizeof(chunk));
    if (!new_chunk) return -1;
    new_chunk->text = strdup_safe(shared_flat);
    if (!new_chunk->text) {
        free(new_chunk);
        return -1;
    }

    printf("[DEBUG apply_flat_insert] AFTER insert: shared_flat = \"%s\"\n", shared_flat);

    new_chunk->next = NULL;

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
    if (!doc || doc->version != version || !content) return -1;

    edit *e = malloc(sizeof(edit));
    if (!e) return -1;
    e->type = EDIT_INSERT;
    e->pos = pos;
    e->text = strdup_safe(content);
    e->len = 0;
    e->next = edit_queue;
    edit_queue = e;
    return 0;
}



// === Formatting Commands ===
int markdown_newline(document *doc, size_t version, size_t pos) {
    if (!doc || doc->version != version) return -1;

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

    return markdown_insert(doc, version, pos, prefix);
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


int markdown_delete(document *doc, uint64_t version, size_t pos, size_t len) {
    if (!doc || doc->version != version || len == 0) return -1;

    edit *e = malloc(sizeof(edit));
    if (!e) return -1;
    e->type = EDIT_DELETE;
    e->pos = pos;
    e->len = len;
    e->text = NULL;
    e->next = edit_queue;
    edit_queue = e;
    return 0;
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
    if (!doc || doc->version != version) return -1;

    ensure_shared_flat_initialized(doc);
    if (!base_flat) return -1;

    char *flat = strdup_safe(base_flat);
    if (!flat) return -1;

    size_t len = strlen(flat);
    size_t line_start = pos;
    while (line_start > 0 && flat[line_start - 1] != '\n') {
        line_start--;
    }

    // Skip digits like "3. "
    size_t i = line_start;
    while (isdigit(flat[i])) i++;
    if (flat[i] == '.' && flat[i + 1] == ' ') {
        i += 2;
    }

    // Find end of line
    size_t line_end = i;
    while (flat[line_end] != '\n' && flat[line_end] != '\0') line_end++;

    size_t delete_len = (flat[line_end] == '\n') ? (line_end - line_start + 1) : (line_end - line_start);
    char *cleaned = strndup_safe(&flat[i], line_end - i);
    if (!cleaned) {
        free(flat);
        return -1;
    }

    printf("[DEBUG blockquote] base_flat:\n%s\n", base_flat);
    printf("[DEBUG blockquote] Found line start: %zu\n", line_start);
    printf("[DEBUG blockquote] Line content: '%.*s'\n", (int)(line_end - line_start), &flat[line_start]);
    printf("[DEBUG blockquote] Cleaned content: '%s'\n", cleaned);
    printf("[DEBUG blockquote] Deleting %zu chars from pos %zu\n", delete_len, line_start);

    if (markdown_delete(doc, version, line_start, delete_len) != 0) {
        printf("[DEBUG blockquote] Failed delete\n");
        free(flat);
        free(cleaned);
        return -1;
    }

    if (markdown_insert(doc, version, line_start, "> ") != 0) {
        printf("[DEBUG blockquote] Failed insert '> '\n");
        free(flat);
        free(cleaned);
        return -1;
    }

    if (markdown_insert(doc, version, line_start + 2, cleaned) != 0) {
        printf("[DEBUG blockquote] Failed insert cleaned content\n");
        free(flat);
        free(cleaned);
        return -1;
    }

    free(flat);
    free(cleaned);
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
    if (!doc || doc->version != version) {
        printf("%s", "[DEBUG hrule] Failed to insert horizontal rule");
        return -1;
    }

    return markdown_insert(doc, version, pos, "---\n");
}

int markdown_link(document *doc, uint64_t version, size_t start, size_t end, const char *url) {
    if (!doc || doc->version != version || start >= end || !url) return -1;

    // Construct the pieces of the link
    char *open_paren = strdup_safe("](");
    char *url_copy = strdup_safe(url);
    char *close_paren = strdup_safe(")");
    char *open_bracket = strdup_safe("[");

    if (!open_paren || !url_copy || !close_paren || !open_bracket) {
        free(open_paren); free(url_copy); free(close_paren); free(open_bracket);
        return -1;
    }

#ifdef DEBUG_MARKDOWN
    char *flat = flatten_staged(doc);
    printf("[DEBUG link] flat before: %s\n", flat);
    printf("[DEBUG link] start = %zu, end = %zu, text to wrap = '%.*s'\n",
           start, end, (int)(end - start), flat + start);
    free(flat);
#endif

    // Insert in reverse order to preserve positions
    if (markdown_insert(doc, version, end, open_paren) != 0) goto fail;
    if (markdown_insert(doc, version, end + strlen(open_paren), url_copy) != 0) goto fail;
    if (markdown_insert(doc, version, end + strlen(open_paren) + strlen(url_copy), close_paren) != 0) goto fail;
    if (markdown_insert(doc, version, start, open_bracket) != 0) goto fail;

#ifdef DEBUG_MARKDOWN
    printf("[DEBUG link] inserted ]( at %zu\n", end);
    printf("[DEBUG link] inserted url at %zu\n", end + strlen(open_paren));
    printf("[DEBUG link] inserted ) at %zu\n", end + strlen(open_paren) + strlen(url_copy));
    printf("[DEBUG link] inserted [ at %zu\n", start);
#endif

    free(open_paren); free(url_copy); free(close_paren); free(open_bracket);
    return 0;

fail:
    free(open_paren); free(url_copy); free(close_paren); free(open_bracket);
    return -1;
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
int compare_insert(const void *a, const void *b) {
    const edit *ea = *(const edit **)a;
    const edit *eb = *(const edit **)b;
    return (int)(ea->pos - eb->pos);
}

int compare_delete(const void *a, const void *b) {
    const edit *ea = *(const edit **)a;
    const edit *eb = *(const edit **)b;
    return (int)(eb->pos - ea->pos);  // reverse order
}
// Helper: count edits
static int count_edits(edit *e) {
    int count = 0;
    while (e) { count++; e = e->next; }
    return count;
}

// Apply all edits
void markdown_increment_version(document *doc) {
    if (!doc) return;

    printf("INC_VERSION: Committing staged to head\n");

    // Snapshot base
    if (base_flat) free(base_flat);
    base_flat = markdown_flatten(doc);
    if (!base_flat) return;

    // --- Apply deletes (already OK) ---
    if (shared_flat) free(shared_flat);
    shared_flat = strdup_safe(base_flat);

    // Apply deletes in reverse order
    edit *curr = edit_queue;
    while (curr) {
        if (curr->type == EDIT_DELETE) {
            size_t total_len = strlen(shared_flat);
            if (curr->pos >= total_len) {
                curr = curr->next;
                continue;
            }

            size_t actual_len = curr->len;
            if (curr->pos + actual_len > total_len)
                actual_len = total_len - curr->pos;

            memmove(shared_flat + curr->pos, shared_flat + curr->pos + actual_len, total_len - curr->pos - actual_len + 1);
        }
        curr = curr->next;
    }

    // --- Collect inserts into array ---
    int n = count_edits(edit_queue);
    edit **insert_edits = malloc(n * sizeof(edit*));
    int idx = 0;
    curr = edit_queue;
    while (curr) {
        if (curr->type == EDIT_INSERT)
            insert_edits[idx++] = curr;
        curr = curr->next;
    }

    // Sort insert_edits by .pos ascending
    for (int i = 0; i < idx - 1; i++) {
        for (int j = i + 1; j < idx; j++) {
            if (insert_edits[i]->pos > insert_edits[j]->pos) {
                edit *tmp = insert_edits[i];
                insert_edits[i] = insert_edits[j];
                insert_edits[j] = tmp;
            }
        }
    }

    // Apply insertions with shifting offset
    size_t offset = 0;
    for (int i = 0; i < idx; i++) {
        edit *e = insert_edits[i];
        size_t insert_len = strlen(e->text);
        size_t old_len = strlen(shared_flat);
        if (e->pos + offset > old_len) e->pos = old_len - offset;

        char *new_flat = malloc(old_len + insert_len + 1);
        memcpy(new_flat, shared_flat, e->pos + offset);
        memcpy(new_flat + e->pos + offset, e->text, insert_len);
        strcpy(new_flat + e->pos + offset + insert_len, shared_flat + e->pos + offset);
        free(shared_flat);
        shared_flat = new_flat;
        offset += insert_len;
    }

    free(insert_edits);

    // Rebuild head
    chunk *new_chunk = malloc(sizeof(chunk));
    new_chunk->text = strdup_safe(shared_flat);
    new_chunk->next = NULL;

    chunk *old = doc->head;
    while (old) {
        chunk *next = old->next;
        free(old->text);
        free(old);
        old = next;
    }
    doc->head = new_chunk;

    // Clear edit queue
    while (edit_queue) {
        edit *next = edit_queue->next;
        if (edit_queue->text) free(edit_queue->text);
        free(edit_queue);
        edit_queue = next;
    }

    doc->version++;
}



#ifdef DEBUG_MARKDOWN
    //gcc -DDEBUG_MARKDOWN markdown.c -o markdown

    //gcc -DDEBUG_MARKDOWN -g -fsanitize=address -o markdown markdown.c
    //leaks --atExit -- ./markdown


    int main() {
// Insert all 3 lines
    document *doc = markdown_init();
    printf("Document initialized\n");

    // Insert the initial sentence
    markdown_insert(doc, 0, 0, "I love cheeseburgers.");
    markdown_increment_version(doc);

    // Bold "cheeseburgers"
    markdown_bold(doc, 1, strlen("I love "), strlen("I love cheeseburgers"));
    markdown_increment_version(doc);

    // Italicize "eese" in cheeseburgers (between ch and se)
    markdown_italic(doc, 2, strlen("I lovch"), strlen("I love cheese"));
    markdown_increment_version(doc);

    // Insert " from BurgerKing" before "."
    char *flat = markdown_flatten(doc);
    char *dot_pos = strchr(flat, '.');
    size_t insert_pos = dot_pos - flat;
    free(flat);
    markdown_insert(doc, 3, insert_pos, " from BurgerKing");
    markdown_increment_version(doc);

    // Delete "BurgerKing"
    flat = markdown_flatten(doc);
    size_t bk_pos = strstr(flat, "BurgerKing") - flat;
    free(flat);
    markdown_delete(doc, 4, bk_pos, strlen("BurgerKing"));
    markdown_increment_version(doc);

    // Insert "McDonald's"
    markdown_insert(doc, 5, bk_pos, "McDonald's");
    markdown_increment_version(doc);

    // Code format "McDonald's"
    markdown_code(doc, 6, bk_pos, bk_pos + strlen("McDonald's"));
    markdown_increment_version(doc);

    // Insert " i'm lovin' it" after "."
    flat = markdown_flatten(doc);
    dot_pos = strchr(flat, '.');
    insert_pos = dot_pos - flat + 1;
    free(flat);
    markdown_insert(doc, 7, insert_pos, " i'm lovin' it");
    markdown_increment_version(doc);

    // Blockquote "i'm lovin' it"
    flat = markdown_flatten(doc);
    size_t quote_pos = strstr(flat, "i'm lovin'") - flat;
    free(flat);
    markdown_blockquote(doc, 8, quote_pos);
    markdown_increment_version(doc);

    // Insert horizontal rule after "."
    flat = markdown_flatten(doc);
    dot_pos = strchr(flat, '.');
    insert_pos = dot_pos - flat + 1;
    free(flat);
    markdown_horizontal_rule(doc, 9, insert_pos);
    markdown_increment_version(doc);

    // Linkify "love" to https://mcdonalds.com.au/
    flat = markdown_flatten(doc);
    size_t love_start = strstr(flat, "love") - flat;
    size_t love_end = love_start + strlen("love");
    free(flat);
    markdown_link(doc, 10, love_start, love_end, "https://mcdonalds.com.au/");
    markdown_increment_version(doc);
    printf("[DEBUG main] love starts at %zu, ends at %zu, text: '%.*s'\n",
        love_start, love_end, (int)(love_end - love_start), flat + love_start);


    // Final output
    char *final = markdown_flatten(doc);
    puts("---- Final Output ----");
    puts(final);
    free(final);
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


