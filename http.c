#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

#include "http.h"

/**
 * String representations for the different HTTP methods
 */
static char *HTTP_METHOD_STRINGS[] = {
        [HTTP_GET] = "GET",
        [HTTP_HEAD] = "HEAD",
        [HTTP_POST] = "POST",
        [HTTP_PUT] = "PUT",
        [HTTP_DELETE] = "DELETE",
        [HTTP_CONNECT] = "CONNECT",
        [HTTP_OPTIONS] = "OPTIONS",
        [HTTP_TRACE] = "TRACE",
        [HTTP_PATCH] = "PATCH"
};

/**
 * @brief Extracts header from stream
 * @param stream Stream aligned to first header
 * @param buf Existing buffer from readline, or null
 * @param line_n Existing line_n from readline, or 0
 * @param header_ln Number of headers will be written into here
 * @param err Error code will be written here on error
 * @return Array of http_header, or NULL on failure
 */
static http_header *extract_header(FILE *stream, char *buf, size_t *line_n, size_t *header_ln, int *err) {
    http_header *header = malloc(2 * sizeof(http_header));
    if (header == NULL) {
        free(header);
        *err = -1;
        return NULL;
    }
    for (size_t i = 0; ; i++) {
        if (getline(&buf, line_n, stream) == -1) {
            free(header);
            *err = errno == EOF ? -3 : -1;
            return NULL;
        }

        if (strcmp(buf, "\r\n") == 0) {
            *header_ln = i;
            return header;
        }

        // if i is power of two, array is full
        if ((i >= 2) && ((i & (i - 1)) == 0)) {
            http_header *new_ptr = realloc(header, 2 * i * sizeof(http_header));
            if (new_ptr == NULL) {
                free(header);
                *err = -1;
                return NULL;
            }
            header = new_ptr;
        }

        size_t delimer = strcspn(buf, ":");
        if (buf[delimer] != ':') {
            free(header);
            *err = -3;
            return NULL;
        }

        char *key = strndup(buf, delimer);
        if (key == NULL) {
            free(header);
            *err = -1;
            return NULL;
        }

        char *value = &buf[delimer + 1 + strspn(&buf[delimer + 1], " ")];
        value = strndup(value,strcspn(value,"\r\n"));
        if (value == NULL) {
            free(header);
            free(key);
            *err = -1;
            return NULL;
        }

        header[i] = (http_header) { .key = key, .value = value };
    }
}

FILE *init_client_conn(char *addr, char *port, const char **err) {
    struct addrinfo req = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    struct addrinfo *pai;
    int res = getaddrinfo(addr, port, &req, &pai);
    if (res == EAI_SYSTEM) {
        return NULL;
    } else if (res != 0) {
        *err = gai_strerror(res);
        return NULL;
    }

    int fd = socket(pai->ai_family, pai->ai_socktype, pai->ai_protocol);
    if (fd == -1) {
        freeaddrinfo(pai);
        return NULL;
    }

    if (connect(fd, pai->ai_addr, pai->ai_addrlen) == -1) {
        freeaddrinfo(pai);
        close(fd);
        return NULL;
    }

    freeaddrinfo(pai);
    FILE *stream = fdopen(fd, "r+");
    if (stream == NULL) {
        close(fd);
        return NULL;
    }

    return stream;
}

int open_socket(char *port, const char **err) {
    struct addrinfo req = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    struct addrinfo *pai;
    int res = getaddrinfo(NULL, port, &req, &pai);
    if (res == EAI_SYSTEM) {
        return -1;
    } else if (res != 0) {
        *err = gai_strerror(res);
        return -1;
    }

    int fd = socket(pai->ai_family, pai->ai_socktype, pai->ai_protocol);
    if (fd == -1) {
        freeaddrinfo(pai);
        return -1;
    }

    int option = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) == -1) {
        freeaddrinfo(pai);
        close(fd);
        return -1;
    }

    if (bind(fd, pai->ai_addr, pai->ai_addrlen) == -1) {
        freeaddrinfo(pai);
        close(fd);
        return -1;
    }

    freeaddrinfo(pai);

    if (listen(fd, 1) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

FILE *accept_client_conn(int socket) {
    int fd = accept(socket, NULL, NULL);
    if (fd == -1) {
        return NULL;
    }

    FILE *stream = fdopen(fd, "r+");
    if (stream == NULL) {
        close(fd);
        return NULL;
    }

    return stream;
}

int send_req(FILE *stream, http_req *req) {
    if (fprintf(stream, "%s /%s HTTP/1.1\r\n", HTTP_METHOD_STRINGS[req->method], req->path[0] == '/' ? &req->path[1] : req->path) < 0) {
        return -1;
    }

    for (size_t i = 0; i < req->header_ln; i++) {
        if (fprintf(stream, "%s: %s\r\n", req->header[i].key, req->header[i].value) < 0) {
            return -1;
        }
    }

    if (req->body != NULL) {
        long old_pos = ftell(req->body);
        fseek(req->body, 0, SEEK_END);
        long file_ln = ftell(req->body);
        fseek(req->body, old_pos, SEEK_SET);

        fprintf(stream, "Content-Length: %ld\r\n", file_ln - old_pos);
    }

    if (fprintf(stream, "Connection: close\r\n\r\n") < 0) {
        return -1;
    }

    if (req->body != NULL) {
        char *buffer[1024];

        while (1) {
            size_t read_ln = fread(buffer, 1024, 1, req->body);
            if (ferror(req->body) != 0) {
                return -1;
            }

            size_t write_ln = fwrite(buffer, read_ln, 1, stream);
            if (write_ln < read_ln) {
                return -1;
            }

            if (read_ln < 1024) {
                break;
            }
        }
    }
    if (fflush(stream) == EOF) {
        return -1;
    }

    return 0;
}

int send_res(FILE *stream, http_res *res) {
    if (fprintf(stream, "HTTP/1.1 %ld %s\r\n", res->status_code.code, res->status_code.description) < 0) {
        return -1;
    }
    for (size_t i = 0; i < res->header_ln; i++) {
        if (fprintf(stream, "%s: %s\r\n", res->header[i].key, res->header[i].value) < 0) {
            return -1;
        }
    }

    char date[256];
    time_t t = time(0);
    struct tm *tmp = gmtime(&t);
    if (tmp == NULL) {
        return -1;
    }
    strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %Z", tmp);

    long length = 0;
    if (res->body != NULL) {
        long old_pos = ftell(res->body);
        fseek(res->body, 0, SEEK_END);
        long file_ln = ftell(res->body);
        fseek(res->body, old_pos, SEEK_SET);

        length = file_ln - old_pos;
    }

    if (fprintf(stream, "Date: %s\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n", date, length) < 0) {
        return -1;
    }

    if (res->body != NULL) {
        char buffer[1024];

        while (1) {
            size_t read_ln = fread(buffer, 1, sizeof(buffer), res->body);
            if (ferror(res->body) != 0) {
                return -1;
            }

            size_t write_ln = fwrite(buffer, 1, read_ln, stream);
            if (write_ln < read_ln) {
                return -1;
            }

            if (read_ln < sizeof(buffer)) {
                break;
            }
        }
    }
    if (fflush(stream) == EOF) {
        return -1;
    }

    return 0;
}

int recv_res(FILE *stream, http_res *res) {
    char *buf = NULL;
    size_t line_n = 0;
    char *saveptr;

    // Head
    if (getline(&buf, &line_n, stream) == -1) {
        free(buf);
        return errno == EOF ? -2 : -1;
    }

    char *token = strtok_r(buf, " ", &saveptr);
    if (token == NULL || strcmp(token, "HTTP/1.1") != 0) {
        free(buf);
        return -2;
    }

    token = strtok_r(NULL, " ", &saveptr);
    if (token == NULL) {
        free(buf);
        return -2;
    }
    char *endptr;
    res->status_code.code = strtol(token, &endptr, 10);
    if (*endptr != '\0') {
        free(buf);
        return -2;
    }

    if (*saveptr != '\0') {
        char *description = strndup(saveptr, strcspn(saveptr, "\r\n"));
        if (description == NULL) {
            free(buf);
            return -1;
        }
        res->status_code.description = description;
    } else {
        res->status_code.description = strdup("");
    }

    // Header
    int err;
    res->header = extract_header(stream, buf, &line_n, &res->header_ln, &err);
    if (res->header == NULL) {
        free(res->status_code.description);
        free(buf);
        return err;
    }

    free(buf);
    return 0;
}

int recv_req(FILE *stream, http_req *req) {
    char *buf = NULL;
    size_t line_n = 0;
    char *saveptr;

    // Head
    if (getline(&buf, &line_n, stream) == -1) {
        free(buf);
        return errno == EOF ? -2 : -1;
    }

    char *token = strtok_r(buf, " ", &saveptr);
    if (token == NULL) {
        free(buf);
        return -2;
    }

    req->method = -1;
    for (int i = 0; i < sizeof(HTTP_METHOD_STRINGS) / sizeof(char*); i++) {
        if (strcmp(token, HTTP_METHOD_STRINGS[i]) == 0) {
            req->method = i;
            break;
        }
    }
    if (req->method == -1) {
        free(buf);
        return -2;
    }

    token = strtok_r(NULL, " ", &saveptr);
    if (token == NULL || token[0] != '/') {
        free(buf);
        return -2;
    }
    req->path = strdup(token);
    if (req->path == NULL) {
        free(buf);
        return -1;
    }

    token = strtok_r(NULL, " ", &saveptr);
    if (token == NULL || strcmp(token, "HTTP/1.1\r\n") != 0) {
        free(req->path);
        free(buf);
        return -2;
    }

    // Header
    int err;
    req->header = extract_header(stream, buf, &line_n, &req->header_ln, &err);
    if (req->header == NULL) {
        free(req->path);
        free(buf);
        return err;
    }

    free(buf);
    return 0;
}

int clear_http_head(FILE *stream) {
    char buf[1024];
    while (fgets(buf, 1024, stream) != NULL) {
        if (strcmp(buf, "\r\n") == 0)
            break;
    }
    return 0;
}

void free_http_res(http_res *res) {
    for (size_t i = 0; i < res->header_ln; i++) {
        free(res->header[i].value);
        free(res->header[i].key);
    }
    free(res->header);
    free(res->status_code.description);
}

void free_http_req(http_req *req) {
    for (size_t i = 0; i < req->header_ln; i++) {
        free(req->header[i].value);
        free(req->header[i].key);
    }
    free(req->header);
    free(req->path);
}
