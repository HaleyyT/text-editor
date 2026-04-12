#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#define FIFO_NAME_MAX 128
#define LINE_MAX 512

static volatile sig_atomic_t g_server_ready = 0;

static void ready_handler(int sig) {
    (void)sig;
    g_server_ready = 1;
}

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

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage:\n"
            "  %s <server_pid> <username>\n"
            "  %s <server_pid> <username> get\n"
            "  %s <server_pid> <username> insert <pos> <text>\n"
            "  %s <server_pid> <username> delete <pos> <len>\n"
            "  %s <server_pid> <username> bold <start> <end>\n"
            "  %s <server_pid> <username> italic <start> <end>\n"
            "  %s <server_pid> <username> heading <level> <pos>\n"
            "  %s <server_pid> <username> newline <pos>\n",
            prog, prog, prog, prog, prog, prog, prog, prog);
}

static int read_and_print_response(int fd_s2c, uint64_t *version_out) {
    char header[LINE_MAX];
    char role[32];
    unsigned long long version_value = 0;
    unsigned long long doc_len = 0;

    if (read_line(fd_s2c, header, sizeof(header)) <= 0) {
        return -1;
    }
    strip_newline(header);

    if (strncmp(header, "ERROR ", 6) == 0) {
        fprintf(stderr, "Server error: %s\n", header + 6);
        return -1;
    }

    if (sscanf(header, "SNAPSHOT %31s %llu %llu", role, &version_value, &doc_len) != 3) {
        fprintf(stderr, "Malformed server response: %s\n", header);
        return -1;
    }

    if (doc_len > 0) {
        char *doc = malloc((size_t)doc_len + 1);
        if (!doc) {
            perror("malloc");
            return -1;
        }

        if (read_full(fd_s2c, doc, (size_t)doc_len) <= 0) {
            free(doc);
            return -1;
        }
        doc[doc_len] = '\0';

        printf("role:%s\nversion:%llu\nlength:%llu\n%s\n",
               role, version_value, doc_len, doc);
        free(doc);
    } else {
        printf("role:%s\nversion:%llu\nlength:%llu\n\n",
               role, version_value, doc_len);
    }

    if (version_out) {
        *version_out = (uint64_t)version_value;
    }

    return 0;
}

int main(int argc, char **argv) {
    pid_t server_pid;
    pid_t client_pid;
    char fifo_c2s[FIFO_NAME_MAX];
    char fifo_s2c[FIFO_NAME_MAX];
    int fd_c2s = -1;
    int fd_s2c = -1;
    struct sigaction sa;
    sigset_t wait_mask;
    sigset_t old_mask;
    uint64_t version = 0;

    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    server_pid = (pid_t)atoi(argv[1]);
    client_pid = getpid();

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = ready_handler;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGUSR2, &sa, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    sigemptyset(&wait_mask);
    sigaddset(&wait_mask, SIGUSR2);
    if (sigprocmask(SIG_BLOCK, &wait_mask, &old_mask) == -1) {
        perror("sigprocmask");
        return 1;
    }

    if (kill(server_pid, SIGUSR1) == -1) {
        perror("kill");
        return 1;
    }

    while (!g_server_ready) {
        sigset_t suspend_mask = old_mask;
        sigdelset(&suspend_mask, SIGUSR2);
        sigsuspend(&suspend_mask);
    }

    if (sigprocmask(SIG_SETMASK, &old_mask, NULL) == -1) {
        perror("sigprocmask restore");
        return 1;
    }

    snprintf(fifo_c2s, sizeof(fifo_c2s), "FIFO_C2S_%d", client_pid);
    snprintf(fifo_s2c, sizeof(fifo_s2c), "FIFO_S2C_%d", client_pid);

    fd_c2s = open(fifo_c2s, O_WRONLY);
    if (fd_c2s < 0) {
        perror("open FIFO_C2S");
        return 1;
    }

    fd_s2c = open(fifo_s2c, O_RDONLY);
    if (fd_s2c < 0) {
        perror("open FIFO_S2C");
        close(fd_c2s);
        return 1;
    }

    if (write_full(fd_c2s, argv[2], strlen(argv[2])) < 0 ||
        write_full(fd_c2s, "\n", 1) < 0) {
        perror("write username");
        close(fd_c2s);
        close(fd_s2c);
        return 1;
    }

    if (read_and_print_response(fd_s2c, &version) != 0) {
        close(fd_c2s);
        close(fd_s2c);
        return 1;
    }

    if (argc == 3) {
        write_full(fd_c2s, "DISCONNECT\n", 11);
        close(fd_c2s);
        close(fd_s2c);
        return 0;
    }

    {
        const char *command = argv[3];
        char request[LINE_MAX];
        const char *payload = "";
        size_t pos = 0;
        size_t len = 0;
        size_t payload_len = 0;

        if (strcmp(command, "get") == 0) {
            if (argc != 4) {
                print_usage(argv[0]);
                goto fail;
            }
        } else if (strcmp(command, "insert") == 0) {
            if (argc != 6) {
                print_usage(argv[0]);
                goto fail;
            }
            pos = (size_t)strtoull(argv[4], NULL, 10);
            payload = argv[5];
            payload_len = strlen(payload);
        } else if (strcmp(command, "delete") == 0 ||
                   strcmp(command, "bold") == 0 ||
                   strcmp(command, "italic") == 0) {
            if (argc != 6) {
                print_usage(argv[0]);
                goto fail;
            }
            pos = (size_t)strtoull(argv[4], NULL, 10);
            len = (size_t)strtoull(argv[5], NULL, 10);
        } else if (strcmp(command, "heading") == 0) {
            if (argc != 6) {
                print_usage(argv[0]);
                goto fail;
            }
            len = (size_t)strtoull(argv[4], NULL, 10);
            pos = (size_t)strtoull(argv[5], NULL, 10);
        } else if (strcmp(command, "newline") == 0) {
            if (argc != 5) {
                print_usage(argv[0]);
                goto fail;
            }
            pos = (size_t)strtoull(argv[4], NULL, 10);
        } else {
            fprintf(stderr, "Unknown command: %s\n", command);
            goto fail;
        }

        snprintf(request, sizeof(request), "REQUEST %s %llu %zu %zu %zu\n",
                 command,
                 (unsigned long long)version,
                 pos,
                 len,
                 payload_len);

        if (write_full(fd_c2s, request, strlen(request)) < 0) {
            perror("write request");
            goto fail;
        }
        if (payload_len > 0 && write_full(fd_c2s, payload, payload_len) < 0) {
            perror("write payload");
            goto fail;
        }

        if (read_and_print_response(fd_s2c, &version) != 0) {
            goto fail;
        }
    }

    write_full(fd_c2s, "DISCONNECT\n", 11);
    close(fd_c2s);
    close(fd_s2c);
    return 0;

fail:
    write_full(fd_c2s, "DISCONNECT\n", 11);
    close(fd_c2s);
    close(fd_s2c);
    return 1;
}
