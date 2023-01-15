/**
 * @file client.c
 * @author Andreas Schloessl (12119901)
 * @date 14.01.2023
 *
 * @brief Basic HTTP Client Implementation
 *
 * @details This is a HTTP Client Implementation for the CLI.
 * It sends a GET request to the specified URL and prints the response.
 * Alternatively, this response can also be written to a file with the -o or -d options.
 */

#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>

#include "http.h"

/**
 * Structure that represents an URL
 */
typedef struct {
    char *url;
    char *host;
    char *path;
} url_t;

/**
 * Structure that represents all passed arguments
 */
typedef struct {
    char *port;
    url_t url;
    char *file;
    char *dir;
} args_t;

/**
 * @brief Prints usage text to stderr
 * @details Print usage text to stderr, using binary name from argument binary.
 * @param binary Represents the name of the binary, should probably be argv[0].
 */
static void print_usage(char *binary) {
    fprintf(stderr, "Usage: %s [-p PORT] [ -o FILE | -d DIR ] URL\n", binary);
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
    args->file = NULL;
    args->dir = NULL;
    args->port = "80";

    // Parse all flags and parameters
    int opt;
    char *endptr = NULL;
    while ((opt = getopt(argc, argv, "p:o:d:")) != -1) {
        switch (opt) {
            case 'p':
                if (endptr != NULL)
                    return -1;

                strtol(optarg, &endptr, 10);
                if (*endptr != '\0')
                    return -1;
                args->port = optarg;
                break;
            case 'o':
                if (args->dir != NULL || args->file != NULL) {
                    return -1;
                }
                args->file = optarg;
                break;
            case 'd':
                if (args->file != NULL || args->dir != NULL) {
                    return -1;
                }
                args->dir = optarg;
                break;
            default:
                return -1;
        }
    }

    if (optind + 1 != argc) {
        return -1;
    }

    args->url.url = argv[optind++];
    args->url.host = NULL;
    args->url.path = NULL;
    return 0;
}

/**
 * @brief Parses URL into host and path.
 * @details Parses URL into host and path and fills the provided url struct with this data.
 * @param url url struct with filled url member
 * @return 0 on success, -1 on failure, -2 on malformed URL
 */
static int parse_url(url_t *url) {
    if (strncmp(url->url, "http://", 7) != 0) {
        return -2;
    }

    unsigned long host_ln = strcspn(&url->url[7], ";/?:@=&");
    url->host = strndup(&url->url[7], host_ln);
    if (url->host == NULL) {
        return -1;
    }

    url->path = strdup(&url->url[7 + host_ln]);
    if (url->path == NULL) {
        return -1;
    }

    return 0;
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

    int err_code = parse_url(&args.url);
    if (err_code != 0) {
        if (err_code == -2) {
            fprintf(stderr, "Invalid URL\n");
        } else {
            perror("Failed to parse URL");
        }
        return EXIT_FAILURE;
    }

    FILE *outStream = NULL;
    if (args.file != NULL) {
        outStream = fopen(args.file, "w");
        if (outStream == NULL) {
            perror("Failed to open file");
            return EXIT_FAILURE;
        }
    }

    if (args.dir != NULL) {
        char *file = strrchr(args.url.path, '/');
        if (file == NULL || file[1] == '\0' || file[1] == '?') {
            file = "index.html";
        } else {
            file = &file[1];
        }

        char *path = malloc((strlen(file) + strlen(args.dir) + 2) * sizeof(char));
        if (path == NULL) {
            perror("Failed to allocate memory");
            return EXIT_FAILURE;
        }
        strcpy(path, args.dir);
        strcat(path, "/");
        strncat(path, file, strcspn(file, "?"));
        outStream = fopen(path, "w");
        free(path);
        if (outStream == NULL) {
            perror("Failed to open file");
            return EXIT_FAILURE;
        }
    }

    const char *err = NULL;
    FILE *stream = init_client_conn(args.url.host, args.port, &err);
    if (stream == NULL) {
        if (err == NULL) {
            perror("Failed to initiate connection");
        } else {
            fprintf(stderr, "Failed to initiate connection: %s\n", err);
        }
        return EXIT_FAILURE;
    }

    http_req req = {
            .path = args.url.path,
            .method = HTTP_GET,
            .header = (http_header[]) { { .key = "Host", .value = args.url.host } },
            .header_ln = 1
    };

    if (send_req(stream, &req) == -1) {
        perror("Failed to send request");
        fclose(stream);
        if (outStream) {
            fclose(outStream);
        }
        return EXIT_FAILURE;
    }

    http_res res;
    err_code = recv_res(stream, &res);
    if (err_code != 0) {
        if (err_code == -2 || err_code == -3) {
            fprintf(stderr, "Protocol error!\n");
            fclose(stream);
            if (outStream) {
                fclose(outStream);
            }
            return 2;
        } else {
            perror("Error while receiving response");
            fclose(stream);
            if (outStream) {
                fclose(outStream);
            }
            return EXIT_FAILURE;
        }
    }

    if (res.status_code.code != 200) {
        fprintf(stderr, "%ld %s\n", res.status_code.code, res.status_code.description);
        free(args.url.host);
        free(args.url.path);
        free_http_res(&res);
        fclose(stream);
        if (outStream) {
            fclose(outStream);
        }
        return 3;
    }

    char buffer[1024];
    while (1) {
        size_t read_ln = fread(buffer, 1, sizeof(buffer), stream);
        if (ferror(stream) != 0) {
            perror("Error while reading stream");
            return EXIT_FAILURE;
        }

        size_t write_ln = fwrite(buffer, 1, read_ln, outStream == NULL ? stdout : outStream);
        if (write_ln < read_ln) {
            perror("Error while writing stream");
            return EXIT_FAILURE;
        }

        if (read_ln < sizeof(buffer)) {
            break;
        }
    }

    free(args.url.host);
    free(args.url.path);
    free_http_res(&res);
    fclose(stream);
    if (outStream) {
        fclose(outStream);
    }
    return 0;
}
