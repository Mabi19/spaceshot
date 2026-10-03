#include "clipboard.h"
#include "args.h"
#include "link-buffer.h"
#include "log.h"
#include "wayland/globals.h"
#include <ext-data-control-client.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>

typedef struct {
    struct ext_data_control_source_v1 *data_source;
    const char *mime_type;
    char *file_uri;
    char *file_path;
    LinkBuffer *data;
    bool *active;
} ClipboardCopy;

static void clipboard_handle_send(
    void *data,
    struct ext_data_control_source_v1 * /* data_source */,
    const char *mime_type,
    int fd
) {
    ClipboardCopy *copy = data;
    log_debug("sending clipboard data\n");

    FILE *wrapped_fd = fdopen(fd, "w");
    if (!wrapped_fd) {
        perror("fdopen");
        close(fd);
        report_error_fatal("couldn't open clipboard fd %d", fd);
    }

    if (strcmp(mime_type, copy->mime_type) == 0) {
        link_buffer_write(copy->data, wrapped_fd);
    } else if (copy->file_uri && strcmp(mime_type, "text/uri-list") == 0) {
        fputs(copy->file_uri, wrapped_fd);
    } else if (
        copy->file_path &&
        (strcmp(mime_type, "text/plain;charset=utf-8") == 0 ||
         strcmp(mime_type, "text/plain") == 0)
    ) {
        fputs(copy->file_path, wrapped_fd);
    } else {
        report_warning("no offer for mime type %s\n", mime_type);
    }

    fclose(wrapped_fd);
}

static void clipboard_handle_cancelled(
    void *data, struct ext_data_control_source_v1 *data_source
) {
    ClipboardCopy *copy = data;
    ext_data_control_source_v1_destroy(data_source);

    if (copy->file_uri) {
        free(copy->file_uri);
    }
    if (copy->file_path) {
        free(copy->file_path);
    }
    link_buffer_destroy(copy->data);

    *(copy->active) = false;
    free(copy);
}

static struct ext_data_control_source_v1_listener clipboard_source_listener = {
    .send = clipboard_handle_send,
    .cancelled = clipboard_handle_cancelled,
};

static struct ext_data_control_device_v1 *get_data_device() {
    if (!wayland_globals.ext_data_control_device) {
        wayland_globals.ext_data_control_device =
            ext_data_control_manager_v1_get_data_device(
                wayland_globals.ext_data_control_manager,
                wayland_globals.seat_dispatcher->seat
            );
    }
    return wayland_globals.ext_data_control_device;
}

static bool should_percent_encode(char c) {
    if (c >= 'A' && c <= 'Z') {
        return false;
    } else if (c >= 'a' && c <= 'z') {
        return false;
    } else if (c >= '0' && c <= '9') {
        return false;
    } else {
        switch (c) {
        case '-':
        case '.':
        case '_':
        case '~':
        case '/':
            return false;
        default:
            return true;
        }
    }
}

static inline char to_hex_digit(int x) {
    return x < 10 ? '0' + x : 'A' + x - 10;
}

static void spawn_clipboard_child(
    const char *mime_type, LinkBuffer *buf, const char *path
) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        report_error("couldn't create a pipe for the clipboard helper");
        return;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        report_error("couldn't spawn the clipboard helper");
        return;
    }

    if (pid == 0) {
        // child
        setsid();

        close(pipefd[1]);
        if (dup2(pipefd[0], STDIN_FILENO) == -1) {
            report_error_fatal(
                "couldn't redirect the clipboard helper's stdin"
            );
        }
        if (pipefd[0] != STDIN_FILENO) {
            close(pipefd[0]);
        }

        int dev_null = open("/dev/null", O_WRONLY);
        if (dev_null != -1) {
            dup2(dev_null, STDOUT_FILENO);
            if (dev_null != STDOUT_FILENO) {
                close(dev_null);
            }
        }

        char *executable = (char *)args.executable_name;
        char *const helper_argv[] = {
            executable,
            "copy-helper",
            (char *)mime_type,
            (char *)path,
            NULL,
        };
        execvp(executable, helper_argv);
        report_error_fatal("couldn't exec the clipboard helper");
    }

    // parent
    close(pipefd[0]);
    FILE *child_stdin = fdopen(pipefd[1], "w");
    if (!child_stdin) {
        close(pipefd[1]);
        report_error("couldn't open the clipboard helper's stdin");
        return;
    }
    link_buffer_write(buf, child_stdin);
    fclose(child_stdin);
}

void clipboard_copy(
    const char *mime_type,
    LinkBuffer *buf,
    const char *path,
    bool *active,
    bool async
) {
    if (async) {
        spawn_clipboard_child(mime_type, buf, path);
        link_buffer_destroy(buf);
        *active = false;
        return;
    }

    ClipboardCopy *copy = calloc(1, sizeof(ClipboardCopy));

    copy->data_source = ext_data_control_manager_v1_create_data_source(
        wayland_globals.ext_data_control_manager
    );
    copy->mime_type = mime_type;
    copy->data = buf;
    copy->active = active;

    struct ext_data_control_device_v1 *data_device = get_data_device();

    ext_data_control_source_v1_offer(copy->data_source, mime_type);
    // a single - means stdout, which is not a file
    if (path && strcmp(path, "-") != 0) {
        // text/uri-list format:
        // "file://" + percent-encode(to-absolute(path)) + CR LF
        char *absolute_path = realpath(path, NULL);
        if (!absolute_path) {
            report_warning("couldn't get absolute path to saved screenshot");
            goto end_copy_path;
        }

        size_t buf_size = sizeof("file://") + 2;
        int src_pos = 0;
        while (absolute_path[src_pos] != '\0') {
            buf_size += should_percent_encode(absolute_path[src_pos]) ? 3 : 1;
            src_pos++;
        }
        copy->file_uri = malloc(buf_size);
        strcpy(copy->file_uri, "file://");
        char *dest = copy->file_uri + 7;
        src_pos = 0;
        while (absolute_path[src_pos] != '\0') {
            char c = absolute_path[src_pos];
            if (should_percent_encode(c)) {
                dest[0] = '%';
                dest[1] = to_hex_digit((c & 0xf0) >> 4);
                dest[2] = to_hex_digit(c & 0x0f);
                dest += 3;
            } else {
                *dest = c;
                dest++;
            }
            src_pos++;
        }
        dest[0] = '\x0d';
        dest[1] = '\x0a';
        dest[2] = '\0';

        ext_data_control_source_v1_offer(copy->data_source, "text/uri-list");
        // Also copy as text/plain, so you can paste the path if the file
        // doesn't work (like in a terminal).
        copy->file_path = absolute_path;
        ext_data_control_source_v1_offer(
            copy->data_source, "text/plain;charset=utf-8"
        );
        ext_data_control_source_v1_offer(copy->data_source, "text/plain");
    }
end_copy_path:

    ext_data_control_source_v1_add_listener(
        copy->data_source, &clipboard_source_listener, copy
    );
    ext_data_control_device_v1_set_selection(data_device, copy->data_source);
    *(copy->active) = true;
}
