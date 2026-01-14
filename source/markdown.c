#include "../libs/markdown.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>


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

    chunk *curr = doc->head;
    while (curr){
        chunk *next = curr->next;
        free(curr->text);
        free(curr);
        curr = next;
    }

    //Traverse through staged_head and free 
    curr = doc->staged_head;
    while (curr) {
        chunk *next = curr->next;
        free(curr->text);
        free(curr);
        curr = next;
    }

    free(doc);
}

/*
* Functions strdup_safe and strndup_safe to copy the content safely
*/
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

    //copy content from head into new_head 
    new_head->text = strdup_safe(head->text);
    if (!new_head->text) {
        free(new_head);
        return NULL;
    }

    new_head->next = deep_copy_chunks(head->next);
    return new_head;
}


/*
 * Helper: Ensures that the shared edit buffer shared_flat is initialised
 * for the current version, allowing deferred edits to be staged
 * against a consistent snapshot base_flat of the document.
 */
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


//Helper function to retrieve a string presentation of doc in its staged version
char *flatten_staged(const document *doc) {
    if (!doc || !doc->staged_head) return strdup_safe("");

    return markdown_flatten(doc);
}


/**
 * Insert directly to the shared_flat string at the position.
 * Used after version increment to update the shared copy of shared_flat with inserted content
 * It rebuilds staged_head based on the modified flat string.
*/
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
    // Clamp insertion position if it goes beyond current buffer
    size_t new_len = len + insert_len;
    char *new_doc = malloc(new_len + 1);
    if (!new_doc) return -1;

    // copy parts before, content to insert, and parts after into new buffer
    memcpy(new_doc, shared_flat, pos);
    memcpy(new_doc + pos, content, insert_len);
    memcpy(new_doc + pos + insert_len, shared_flat + pos, len - pos);
    new_doc[new_len] = '\0';

    free(shared_flat);
    shared_flat = new_doc;

    // Rebuild staged_head, the new chunk contains updated content of shared_flat
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



/**
 * Stages an insert operation in the document at the given version
 * 
 * The insert is deferred and stored in an edit queue, and will only be applied
 * when`markdown_increment_version() is called. This allows multiple changes
 * to be grouped and committed together.
 */
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

//Insert a newline character at the given position
int markdown_newline(document *doc, size_t version, size_t pos) {
    if (!doc || doc->version != version) return -1;

    return markdown_insert(doc, version, pos, "\n");
}

/**
 * Insert a heading format at the given position
 * 
 * Converts the line at the given position into a heading by prepending it with
 * level number of '#'. The change is staged and will appear in next version increment
 */
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



/**
 * Stages bold formatting for the specified range of text in the document.
 *
 * This function wraps the text "**" at start and end of the text.
 * The formatting is staged and applied when incrementing version
*/
int markdown_bold(document *doc, uint64_t version, size_t start, size_t end) {
    if (!doc || doc->version != version || start >= end) return -1;

    if (!doc->staged_head) {
        doc->staged_head = deep_copy_chunks(doc->head);
        if (!doc->staged_head) return -1;
    }

    // Insert "**" at the end first to avoid shifting start position
    if (markdown_insert(doc, version, end, "**") != 0) {
        printf("%s", "make bold text fail");
        return -1;
    }
    
    // Insert "**" at the start position 
    if (markdown_insert(doc, version, start, "**") != 0) {
        printf("%s", "make bold text fail");
        return -1;
    }

    return SUCCESS;
}


/**
 * Stages a delete operation that removes "len" characters starting at "pos"
 *
 * The deletion is deferred and added to the edit queue, which will be applied
 * when the document version is incremented. It allows safe sequencing with other edits.
*/
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


/**
 * Formatting italic text by inserting "*" the specified range of text
 * Wraps a single asterisks "*" at start and end position.
 */
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

    // Insert "*" at the start position 
    if (markdown_insert(doc, version, start, "*") != 0) {
        printf("%s", "ITALIC: unable to italicise");
        return -1; 
    }
    return SUCCESS;
}


/**
 * Converts the line at the given position into a blockquote.
 *
 * Finds the start of the line containing "pos", remove if there is prefix, 
 * and prepends it with the blockquote "> ". 
 * If not already on a newline, it inserts one before the blockquote.
 * All changes are staged and applied at version increment.
*/
int markdown_blockquote(document *doc, uint64_t version, size_t pos) {
    if (!doc || doc->version != version) return -1;

    ensure_shared_flat_initialized(doc);
    if (!base_flat) return -1;

    char *flat = strdup_safe(base_flat);
    if (!flat) return -1;

    size_t line_start = pos;
    while (line_start > 0 && flat[line_start - 1] != '\n') {
        line_start--;
    }

    // Skip teh prefix 
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

    //inserts newline before the blockquote if it does not have
    if (line_start != 0 && flat[line_start - 1] != '\n') {
        if (markdown_insert(doc, version, line_start, "\n") != 0) {
            printf("[DEBUG blockquote] Failed insert newline before blockquote\n");
            free(flat);
            free(cleaned);
            return -1;
        }
        line_start += 1;  // shift right because inserted a char
    }

    //insert thethe blockquote "> " at the start of the line 
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


/**
 * Converts multiple lines starting at a given position into an ordered list.
 * 
 * This function adds numeric prefixes to each line at the specified position. 
 * It iterates through staged content, detects line breaks, and inserts the appropriate prefix.
 */
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
        snprintf(prefix, sizeof(prefix), "%d. ", index); 

        //insert the prefix at the position 
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



/**
 * Converts multiple lines starting at the given position into an unordered list.
 * 
 * This function inserts "- " at the start of each line found in the staged document,
 * It ensures each insertion occurs at the beginning of a line, 
 * optionally inserting a newline if necessary to preserve line alignment.
 */
int markdown_unordered_list(document *doc, uint64_t version, size_t pos) {
    if (!doc || doc->version != version) return -1;

    //copy the head chunk into staged_head
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
    size_t insert_at = i + shift;

    // Insert \n before "- " if not already on a new line
    if (insert_at != 0 && str_flat[insert_at - 1] != '\n') {
        if (markdown_insert(doc, version, insert_at, "\n") != 0) {
            printf("fail to insert newline at position: %zu\n", insert_at);
            free(str_flat);
            return -1;
        }
        insert_at++;
        shift++;
    }

    //inserting "- " at at pos
    printf("Inserting \"- \" at position: %zu\n", insert_at);
    if (markdown_insert(doc, version, insert_at, "- ") != 0) {
        printf("fail to insert - sign at position: %zu\n", insert_at);
        free(str_flat);
        return -1;
    }
    shift += 2;

    // Move to next line
    while (i < len && str_flat[i] != '\n') i++;
    i++; // move past \n 
}

    char *check = flatten_staged(doc);
    printf("Staged content after list formatting:\n%s\n", check);
    free(check);

    free(str_flat);
    return SUCCESS;
}


/**
 * Formats a range of text as inline code using backticks.
 * Wraps the text between start and end with a single backtick character (`).
 */
int markdown_code(document *doc, uint64_t version, size_t start, size_t end) {
    if (!doc || doc->version != version || start >= end) {
        printf("%s"," Markdown_code: conditions not met \n");
        return -1;
    }

    if (!doc->staged_head) {
        doc->staged_head = deep_copy_chunks(doc->head);
        if (!doc->staged_head) return -1;
    }

    // Insert opening backtick first to prevent shifting
    if (markdown_insert(doc, version, end, "`") != 0) {
        printf("%s"," Markdown_code: fail inserting ' at end \n");
        return -1;
    }

    //Insert backtick at the start position 
    if (markdown_insert(doc, version, start, "`") != 0) {
        printf("%s"," Markdown_code: fail inserting ' at start \n");
        return -1;
    }
    return SUCCESS;
}


/**
 * Inserts a horizontal rule (---) at the specified position.
 * Ensures a horizontal rule is placed on a separate line by 
 * inserting newlines before and after if necessary.
 */
int markdown_horizontal_rule(document *doc, uint64_t version, size_t pos) {
    if (!doc || doc->version != version) return -1;

    ensure_shared_flat_initialized(doc);
    size_t len = strlen(shared_flat);

    // Insert newline before if not at start of line
    if (pos > 0 && shared_flat[pos - 1] != '\n') {
        if (markdown_insert(doc, version, pos, "\n") != 0) return -1;
        pos++; // adjust position for next insert
    }

    // Insert the horizontal rule
    if (markdown_insert(doc, version, pos, "---") != 0) return -1;
    pos += 3;

    // Insert newline after if not already present
    if (pos >= len || shared_flat[pos] != '\n') {
        if (markdown_insert(doc, version, pos, "\n") != 0) return -1;
    }

    return 0;
}


/**
 * Wraps a substring in a hyperlink format "[text](url)". 
 * Insert in reverse order to maintain correct position under deferred semantics. 
 */
int markdown_link(document *doc, uint64_t version, size_t start, size_t end, const char *url) {
    if (!doc || doc->version != version || start >= end || !url) return -1;

    ensure_shared_flat_initialized(doc);
    if (!shared_flat) return -1;

    char *flat = strdup_safe(shared_flat);
    if (!flat) return -1;

    printf("[DEBUG link] shared_flat before insert: \"%s\"\n", flat);
    printf("[DEBUG link] Intended range: [%zu, %zu), text='%.*s'\n", 
           start, end, (int)(end - start), flat + start);

    // Sanity check: make sure we're wrapping the correct word
    if (strncmp(flat + start, "love", 4) != 0) {
        printf("[DEBUG link] Warning: link range does not match expected word 'love'\n");
    }
    free(flat);

    // Apply in reverse order to preserve index integrity
    if (markdown_insert(doc, version, end, ")") != 0) return -1;
    if (markdown_insert(doc, version, end, url) != 0) return -1;
    if (markdown_insert(doc, version, end, "](") != 0) return -1;
    if (markdown_insert(doc, version, start, "[") != 0) return -1;

    // Print staged content after inserts
    char *after = flatten_staged(doc);
    printf("[DEBUG link] staged content after link insert: \"%s\"\n", after);
    free(after);

    return 0;
}




// === Utilities ===
void markdown_print(const document *doc, FILE *stream) {
    (void)doc; (void)stream;
}


/**
 * Flattens the committed version of the document into a single string.
 *
 * Traverses the linked list of chunks in the "head" field and concatenates
 * contents into a new allocated string. Used for generating output or 
 * computing positions for future edits. This function reflects the committed state.
 */
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
/**
 * Comparator used to sort insert edits in ascending position order.
 * Used when applying edits in a consistent order, ensures that earlier 
 * insertions are applied first to maintain semantic correctness.
 */
int compare_insert(const void *a, const void *b) {
    const edit *ea = *(const edit **)a;
    const edit *eb = *(const edit **)b;
    return (int)(ea->pos - eb->pos);
}

/**
 * Comparator used to sort delete edits in descending position order.
 * Reversing the delete order ensures that subsequent deletes don't shift the
 * positions of earlier ones, which would corrupt range of edits.
 */
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


/*
 * Commits all staged edits into the document.
 * First applies deletions in reverse order.
 * Then applies insertions in ascending order, adjusting for offset.
 * Updates head and resets the staging buffer and edit queue.
 */
void markdown_increment_version(document *doc) {
    if (!doc) return;

    printf("INC_VERSION: Committing staged to head\n");

    // Snapshot the current commited state
    if (base_flat) free(base_flat);
    base_flat = markdown_flatten(doc);
    if (!base_flat) return;

    // Prepare the flat representation to apply edits on
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

            //calculate length to prevent overflow
            size_t actual_len = curr->len;
            if (curr->pos + actual_len > total_len)
                actual_len = total_len - curr->pos;

            //delete using memmove
            memmove(shared_flat + curr->pos, shared_flat + curr->pos + actual_len, total_len - curr->pos - actual_len + 1);
        }
        curr = curr->next;
    }

    // Collect inserts into array
    int n = count_edits(edit_queue);
    edit **insert_edits = malloc(n * sizeof(edit*));
    int idx = 0;
    curr = edit_queue;
    while (curr) {
        if (curr->type == EDIT_INSERT)
            insert_edits[idx++] = curr;
        curr = curr->next;
    }

    // Sort insert_edits by position 
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

        // Clamp position to prevent writing past end
        if (e->pos + offset > old_len) e->pos = old_len - offset;

        // allocate new buffer for result
        char *new_flat = malloc(old_len + insert_len + 1);
        memcpy(new_flat, shared_flat, e->pos + offset);
        memcpy(new_flat + e->pos + offset, e->text, insert_len);
        strcpy(new_flat + e->pos + offset + insert_len, shared_flat + e->pos + offset);
        free(shared_flat);
        shared_flat = new_flat;
        offset += insert_len;
    }

    free(insert_edits);

    // Rebuild committed document - head
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

    // Step 1: flat string
    markdown_insert(doc, 0, 0, "112233445566778899");
    markdown_increment_version(doc); // v1

    // Step 2: insert newlines manually (simulate how ED would have split them)
    markdown_insert(doc, 1, 3, "\n");   // after 112
    markdown_insert(doc, 1, 12, "\n");  // after 233445566778
    markdown_increment_version(doc);   // v2

    // Step 3: format 2nd and 3rd lines
    markdown_unordered_list(doc, 2, 4);   // start of line 2
    markdown_unordered_list(doc, 2, 18);  // start of line 3 (adjusted for earlier inserts)
    markdown_increment_version(doc);     // v3

    char *final = markdown_flatten(doc);
    puts("---- Final Output ----");
    puts(final);
    free(final);

    markdown_free(doc);
    return 0;
}
#endif


