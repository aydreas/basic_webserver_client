/**
 * @file server.c
 * @author Andreas Schloessl (12119901)
 * @date 14.01.2023
 *
 * @brief Basic HTTP Server Implementation
 *
 * @details This is a HTTP Server Implementation.
 * Response with data in a file. The file path is calculated based on the path in the URL and DOC_ROOT.
 */

#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include "http.h"

/**
 * Structure that represents all passed arguments
 */
typedef struct {
    char *port;
    char *doc_root;
    char *index;
} args_t;

/**
 * @brief Prints usage text to stderr
 * @details Print usage text to stderr, using binary name from argument binary.
 * @param binary Represents the name of the binary, should probably be argv[0].
 */
static void print_usage(char *binary) {
    fprintf(stderr, "Usage: %s [-p PORT] [-i INDEX] DOC_ROOT\n", binary);
}

/**
 * Parse arguments
 * @brief Parses arguments from the command line
 * @details Parses the arguments p,o and d.
 * @param argc argc which gets passed to main
 * @param argv argv which gets passed to main
 * @param graph initialized args_t struct where the parsed arguments will be stored
 * @return 0 on success, -1 on failure
 */
static int parse_args(int argc, char **argv, args_t *args) {
    // Define defaults if they are not set below
    args->index = NULL;
    args->port = "8080";

    // Parse all flags and parameters
    int opt;
    char *endptr = NULL;
    while ((opt = getopt(argc, argv, "p:i:")) != -1) {
        switch (opt) {
            case 'p':
                if (endptr != NULL)
                    return -1;

                strtol(optarg, &endptr, 10);
                if (*endptr != '\0')
                    return -1;
                args->port = optarg;
                break;
            case 'i':
                if (args->index != NULL) {
                    return -1;
                }
                args->index = optarg;
                break;
            default:
                return -1;
        }
    }

    if (args->index == NULL) {
        args->index = "index.html";
    }

    if (optind + 1 != argc) {
        return -1;
    }

    args->doc_root = argv[optind++];
    return 0;
}

volatile int pending_signal = 0;

/**
 * @brief Handler for receiving signals
 * @details Sets the global variable pending_signal to received signal code
 * @param signum received signal code
 */
static void sig_handler(int signum) {
    pending_signal = signum;
}

/**
 * Main entrypoint.
 * @brief Main entry point
 * @details Main entry point. This is where the program will start from.
 * @param argc argc passed to program
 * @param argv argv passed to program
 * @return exit code
 */
int main(int argc, char **argv) {
    args_t args;
    if (parse_args(argc, argv, &args) == -1) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *err = NULL;
    int socket = open_socket(args.port, &err);
    if (socket == -1) {
        if (err == NULL) {
            perror("Failed to open socket");
        } else {
            fprintf(stderr, "Failed to open socket: %s\n", err);
        }
        return EXIT_FAILURE;
    }

    struct sigaction sa = { .sa_handler = sig_handler };

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    while (pending_signal == 0) {
        FILE *connection = accept_client_conn(socket);
        if (connection == NULL) {
            if (errno != EINTR) {
                perror("Failed to initiate client connection");
            }
            continue;
        }

        http_req req;
        int err_code = recv_req(connection, &req);
        if (err_code == -2 || err_code == -3) {
            fprintf(stderr, "Received malformed packet\n");
            clear_http_head(connection);
            http_res res = { .body = NULL, .header_ln = 0, .status_code = { .code = 400, .description = "Bad Request" } };
            if (send_res(connection, &res) == -1) {
                perror("Failed to send response");
                fclose(connection);
                continue;
            }
            fclose(connection);
            continue;
        } else if (err_code != 0) {
            perror("Error while reading request");
            fclose(connection);
            continue;
        }

        if (req.method != HTTP_GET) {
            http_res res = { .body = NULL, .header_ln = 0, .status_code = { .code = 501, .description = "Not implemented" } };
            if (send_res(connection, &res) == -1) {
                perror("Failed to send response");
                free_http_req(&req);
                fclose(connection);
                continue;
            }
        }

        char *path;
        if (strlen(req.path) == 0) {
            path = malloc(strlen(args.doc_root) + strlen(args.index) + 2);
            if (path == NULL) {
                perror("Failed to allocate memory");
                free_http_req(&req);
                fclose(connection);
                close(socket);
                return -1;
            }
            sprintf(path, "%s/%s", args.doc_root, args.index);
        } else if (req.path[strlen(req.path) - 1] == '/') {
            path = malloc(strlen(args.doc_root) + strlen(req.path) + strlen(args.index) + 1);
            if (path == NULL) {
                perror("Failed to allocate memory");
                free_http_req(&req);
                fclose(connection);
                close(socket);
                return -1;
            }
            sprintf(path, "%s%s%s", args.doc_root, req.path, args.index);
        } else {
            path = malloc(strlen(args.doc_root) + strlen(req.path) + 1);
            if (path == NULL) {
                perror("Failed to allocate memory");
                free_http_req(&req);
                fclose(connection);
                close(socket);
                return -1;
            }
            sprintf(path, "%s%s", args.doc_root, req.path);
        }

        free_http_req(&req);

        FILE *body = fopen(path, "r");
        if (body == NULL) {
            if (errno == ENOENT) {
                http_res res = { .body = NULL, .header_ln = 0, .status_code = { .code = 404, .description = "Not Found" } };
                if (send_res(connection, &res) == -1) {
                    perror("Failed to send response");
                }
                free(path);
                fclose(connection);
                continue;
            } else if (errno == EACCES) {
                http_res res = { .body = NULL, .header_ln = 0, .status_code = { .code = 403, .description = "Forbidden" } };
                if (send_res(connection, &res) == -1) {
                    perror("Failed to send response");
                }
                free(path);
                fclose(connection);
                continue;
            } else {
                http_res res = { .body = NULL, .header_ln = 0, .status_code = { .code = 500, .description = "Internal Server Error" } };
                perror("Failed to access file");
                if (send_res(connection, &res) == -1) {
                    perror("Failed to send response");
                }
                free(path);
                fclose(connection);
                continue;
            }
        }

        char *extension = strrchr(path, '.');
        char *mime = NULL;

        if (extension != NULL) {
            if (strcmp(extension, ".html") == 0 || strcmp(extension, ".htm") == 0) {
                mime = "text/html";
            } else if (strcmp(extension, ".css") == 0) {
                mime = "text/css";
            } else if (strcmp(extension, ".js") == 0) {
                mime = "application/javascript";
            }
        }

        http_res res;
        if (mime != NULL) {
            res = (http_res) {
                .body = body,
                .header_ln = 1,
                .status_code = { .code = 200, .description = "OK" },
                .header = (http_header[]) { { .key = "Content-Type", .value = mime } }
            };
        } else {
            res = (http_res) { .body = body, .header_ln = 0, .status_code = { .code = 200, .description = "OK" } };
        }
        if (send_res(connection, &res) == -1) {
            perror("Failed to send response");
        }
        free(path);
        fclose(body);
        fclose(connection);
    }

    close(socket);
    return 0;
}
