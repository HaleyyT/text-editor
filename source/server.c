#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../libs/markdown.h"

#define USERNAME_MAX 64
#define ROLE_MAX 16
#define FIFO_NAME_MAX 128
#define LINE_MAX 512

typedef enum {
    ROLE_NONE = 0,
    ROLE_READ,
    ROLE_WRITE
} client_role_t;

typedef struct {
    pid_t client_pid;
} client_thread_arg_t;

static int g_signal_pipe[2] = {-1, -1};
static document *g_doc = NULL;
static pthread_mutex_t g_doc_mutex = PTHREAD_MUTEX_INITIALIZER;

static ssize_t write_full(int fd, const void *buf, size_t count) {
    const char *cursor = (const char *)buf;
    size_t written = 0;

    while (written < count) {
        ssize_t rc = write(fd, cursor + written, count - written);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        written += (size_t)rc;
    }

    return (ssize_t)written;
}

static ssize_t read_full(int fd, void *buf, size_t count) {
    char *cursor = (char *)buf;
    size_t total = 0;

    while (total < count) {
        ssize_t rc = read(fd, cursor + total, count - total);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (rc == 0) {
            return 0;
        }
        total += (size_t)rc;
    }

    return (ssize_t)total;
}

static ssize_t read_line(int fd, char *buf, size_t capacity) {
    size_t used = 0;

    if (capacity < 2) {
        errno = EINVAL;
        return -1;
    }

    while (used < capacity - 1) {
        char ch;
        ssize_t rc = read(fd, &ch, 1);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (rc == 0) {
            if (used == 0) {
                return 0;
            }
            break;
        }

        buf[used++] = ch;
        if (ch == '\n') {
            break;
        }
    }

    buf[used] = '\0';
    return (ssize_t)used;
}

static void strip_newline(char *text) {
    size_t len;

    if (!text) {
        return;
    }

    len = strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
        text[--len] = '\0';
    }
}

static void unlink_fifo_if_exists(const char *path) {
    if (unlink(path) == -1 && errno != ENOENT) {
        perror("unlink");
    }
}

static const char *role_to_string(client_role_t role) {
    if (role == ROLE_WRITE) {
        return "write";
    }
    if (role == ROLE_READ) {
        return "read";
    }
    return "unknown";
}

static int lookup_role(const char *username, client_role_t *role_out) {
    FILE *roles_file;
    char line[LINE_MAX];

    roles_file = fopen("roles.txt", "r");
    if (!roles_file) {
        return 0;
    }

    while (fgets(line, sizeof(line), roles_file) != NULL) {
        char file_user[USERNAME_MAX];
        char file_role[ROLE_MAX];

        if (sscanf(line, "%63s %15s", file_user, file_role) != 2) {
            continue;
        }
        if (strcmp(file_user, username) != 0) {
            continue;
        }

        if (strcmp(file_role, "read") == 0) {
            *role_out = ROLE_READ;
            fclose(roles_file);
            return 1;
        }
        if (strcmp(file_role, "write") == 0) {
            *role_out = ROLE_WRITE;
            fclose(roles_file);
            return 1;
        }
    }

    fclose(roles_file);
    return 0;
}

static int send_error(int fd, const char *message) {
    char line[LINE_MAX];

    snprintf(line, sizeof(line), "ERROR %s\n", message);
    return (write_full(fd, line, strlen(line)) < 0) ? -1 : 0;
}

static int send_snapshot_locked(int fd, client_role_t role) {
    char header[LINE_MAX];
    char *flat;
    size_t flat_len;

    flat = markdown_flatten(g_doc);
    if (!flat) {
        return send_error(fd, "INTERNAL");
    }

    flat_len = strlen(flat);
    snprintf(header, sizeof(header), "SNAPSHOT %s %llu %zu\n",
             role_to_string(role),
             (unsigned long long)g_doc->version,
             flat_len);

    if (write_full(fd, header, strlen(header)) < 0 ||
        write_full(fd, flat, flat_len) < 0) {
        free(flat);
        return -1;
    }

    free(flat);
    return 0;
}

static int apply_command_locked(const char *command,
                                uint64_t base_version,
                                size_t pos,
                                size_t len,
                                const char *payload,
                                client_role_t role,
                                int fd_s2c) {
    int rc = -1;

    if (strcmp(command, "get") == 0) {
        return send_snapshot_locked(fd_s2c, role);
    }

    if (role != ROLE_WRITE) {
        return send_error(fd_s2c, "READ_ONLY");
    }

    if (base_version != g_doc->version) {
        return send_error(fd_s2c, "STALE_VERSION");
    }

    if (strcmp(command, "insert") == 0) {
        rc = markdown_insert(g_doc, base_version, pos, payload ? payload : "");
    } else if (strcmp(command, "delete") == 0) {
        rc = markdown_delete(g_doc, base_version, pos, len);
    } else if (strcmp(command, "bold") == 0) {
        rc = markdown_bold(g_doc, base_version, pos, len);
    } else if (strcmp(command, "italic") == 0) {
        rc = markdown_italic(g_doc, base_version, pos, len);
    } else if (strcmp(command, "heading") == 0) {
        rc = markdown_heading(g_doc, base_version, len, pos);
    } else if (strcmp(command, "newline") == 0) {
        rc = markdown_newline(g_doc, base_version, pos);
    } else {
        return send_error(fd_s2c, "UNKNOWN_COMMAND");
    }

    if (rc != 0) {
        return send_error(fd_s2c, "INVALID_EDIT");
    }

    markdown_increment_version(g_doc);
    return send_snapshot_locked(fd_s2c, role);
}

static void connect_signal_handler(int sig, siginfo_t *info, void *context) {
    pid_t pid;

    (void)sig;
    (void)context;

    if (!info) {
        return;
    }

    pid = info->si_pid;
    (void)write(g_signal_pipe[1], &pid, sizeof(pid));
}

static void *client_thread_main(void *arg) {
    client_thread_arg_t *thread_arg = (client_thread_arg_t *)arg;
    pid_t client_pid = thread_arg->client_pid;
    char fifo_c2s[FIFO_NAME_MAX];
    char fifo_s2c[FIFO_NAME_MAX];
    int fd_c2s = -1;
    int fd_s2c = -1;
    client_role_t role = ROLE_NONE;
    char username[USERNAME_MAX];
    char line[LINE_MAX];

    free(thread_arg);

    snprintf(fifo_c2s, sizeof(fifo_c2s), "FIFO_C2S_%d", client_pid);
    snprintf(fifo_s2c, sizeof(fifo_s2c), "FIFO_S2C_%d", client_pid);

    unlink_fifo_if_exists(fifo_c2s);
    unlink_fifo_if_exists(fifo_s2c);

    if (mkfifo(fifo_c2s, 0666) == -1) {
        perror("mkfifo FIFO_C2S");
        return NULL;
    }
    if (mkfifo(fifo_s2c, 0666) == -1) {
        perror("mkfifo FIFO_S2C");
        unlink_fifo_if_exists(fifo_c2s);
        return NULL;
    }

    if (kill(client_pid, SIGUSR2) == -1) {
        perror("kill SIGUSR2");
        goto cleanup;
    }

    fd_s2c = open(fifo_s2c, O_RDWR);
    if (fd_s2c < 0) {
        perror("open FIFO_S2C");
        goto cleanup;
    }

    fd_c2s = open(fifo_c2s, O_RDONLY);
    if (fd_c2s < 0) {
        perror("open FIFO_C2S");
        goto cleanup;
    }

    if (read_line(fd_c2s, username, sizeof(username)) <= 0) {
        goto cleanup;
    }
    strip_newline(username);

    if (!lookup_role(username, &role)) {
        (void)send_error(fd_s2c, "UNAUTHORISED");
        goto cleanup;
    }

    pthread_mutex_lock(&g_doc_mutex);
    if (send_snapshot_locked(fd_s2c, role) < 0) {
        pthread_mutex_unlock(&g_doc_mutex);
        goto cleanup;
    }
    pthread_mutex_unlock(&g_doc_mutex);

    while (1) {
        char command[ROLE_MAX];
        unsigned long long version_value = 0;
        unsigned long long pos_value = 0;
        unsigned long long len_value = 0;
        unsigned long long payload_len = 0;
        char *payload = NULL;

        if (read_line(fd_c2s, line, sizeof(line)) <= 0) {
            break;
        }
        strip_newline(line);

        if (strcmp(line, "DISCONNECT") == 0) {
            break;
        }

        if (sscanf(line, "REQUEST %15s %llu %llu %llu %llu",
                   command,
                   &version_value,
                   &pos_value,
                   &len_value,
                   &payload_len) != 5) {
            (void)send_error(fd_s2c, "BAD_REQUEST");
            continue;
        }

        if (payload_len > 0) {
            payload = calloc((size_t)payload_len + 1, 1);
            if (!payload) {
                (void)send_error(fd_s2c, "INTERNAL");
                continue;
            }

            if (read_full(fd_c2s, payload, (size_t)payload_len) <= 0) {
                free(payload);
                break;
            }
        }

        pthread_mutex_lock(&g_doc_mutex);
        if (apply_command_locked(command,
                                 (uint64_t)version_value,
                                 (size_t)pos_value,
                                 (size_t)len_value,
                                 payload,
                                 role,
                                 fd_s2c) < 0) {
            pthread_mutex_unlock(&g_doc_mutex);
            free(payload);
            break;
        }
        pthread_mutex_unlock(&g_doc_mutex);

        free(payload);
    }

cleanup:
    if (fd_c2s >= 0) {
        close(fd_c2s);
    }
    if (fd_s2c >= 0) {
        close(fd_s2c);
    }
    unlink_fifo_if_exists(fifo_c2s);
    unlink_fifo_if_exists(fifo_s2c);
    return NULL;
}

int main(int argc, char **argv) {
    struct sigaction sa;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <time_interval_seconds>\n", argv[0]);
        return 1;
    }

    if (pipe(g_signal_pipe) == -1) {
        perror("pipe");
        return 1;
    }

    g_doc = markdown_init();
    if (!g_doc) {
        perror("markdown_init");
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = connect_signal_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        markdown_free(g_doc);
        return 1;
    }

    printf("Server PID: %d\n", getpid());
    fflush(stdout);

    while (1) {
        client_thread_arg_t *thread_arg;
        pthread_t thread_id;
        pid_t client_pid;

        if (read_full(g_signal_pipe[0], &client_pid, sizeof(client_pid)) <= 0) {
            continue;
        }

        thread_arg = malloc(sizeof(*thread_arg));
        if (!thread_arg) {
            continue;
        }
        thread_arg->client_pid = client_pid;

        if (pthread_create(&thread_id, NULL, client_thread_main, thread_arg) != 0) {
            free(thread_arg);
            continue;
        }
        pthread_detach(thread_id);
    }

    return 0;
}
