#ifndef UE3_HTTP_H
#define UE3_HTTP_H

/**
 * Enum representing the different HTTP Methods
 */
typedef enum {
    HTTP_GET,
    HTTP_HEAD,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_CONNECT,
    HTTP_OPTIONS,
    HTTP_TRACE,
    HTTP_PATCH
} http_method;

/**
 * Struct representing an HTTP header
 */
typedef struct {
    char *key;
    char *value;
} http_header;

/**
 * Struct representing an HTTP status code
 */
typedef struct {
    long code;
    char *description;
} http_status_code;

/**
 * Struct representing an HTTP request
 */
typedef struct {
    http_method method;
    char *path;
    http_header *header; // array
    size_t header_ln;
    FILE *body; // only for sending
} http_req;

/**
 * Struct representing an HTTP response
 */
typedef struct {
    http_status_code status_code;
    http_header *header; // array
    size_t header_ln;
    FILE *body; // only for sending
} http_res;

/**
 * @brief Initiates a client connection
 * @details Initiates a client connection to addr and port and returns an open file descriptor for the socket.
 * @param addr remote address to connect to
 * @param port remote port to connect to
 * @param err error message - is populated if NULL is returned and errno is not set
 * @return file handle for the socket, or NULL if failed
 */
FILE *init_client_conn(char *addr, char *port, const char **err);

/**
 *
 * @param port
 * @param err
 * @return
 */
int open_socket(char *port, const char **err);

/**
 * @brief Accepts a client connection
 * @details Accepts a waiting client connection. Blocks until a client is available.
 * @param socket File directive to listening socket
 * @return File handle of client connection, NULL on failure
 */
FILE *accept_client_conn(int socket);

/**
 * @brief Sends an HTTP request
 * @details Sends an HTTP request described by the req parameter.
 * @param stream open socket
 * @param req filled http_req struct which describes the http request
 * @return 0 on success, -1 on failure
 */
int send_req(FILE *stream, http_req *req);

/**
 * @brief Sends an HTTP response
 * @details Sends an HTTP response described by the res parameter.
 * @param stream open socket
 * @param req filled http_res struct which describes the http response
 * @return 0 on success, -1 on failure
 */
int send_res(FILE *stream, http_res *res);

/**
 * @brief Waits for and receives an HTTP request
 * @details Waits for and receives an HTTP response and saved the data into res
 * @param stream open socket
 * @param res empty http_req struct. It will be filled with the request data
 * @return 0 on success, -1 on failure, -2 on malformed HTTP head, -3 on malformed headers
 */
int recv_req(FILE *stream, http_req *req);

/**
 * @brief Waits for and receives an HTTP response
 * @details Waits for and receives an HTTP response and saved the data into res
 * @param stream open socket
 * @param res empty http_res struct. It will be filled with the response data
 * @return 0 on success, -1 on failure, -2 on malformed HTTP head, -3 on malformed headers
 */
int recv_res(FILE *stream, http_res *res);

/**
 * @brief Reads from stream until end of HTTP header
 * @details Reads from stream until end of HTTP header. This occurs either when the stream is closed with no data left,
 * or when receiving an empty line
 * @param stream to clear
 * @return 0 on success, -1 on failure
 */
int clear_http_head(FILE *stream);

/**
 * @brief Frees a filled up http_req struct
 * @details Frees all memory allocations used by a populated http_req struct
 * @param res filled http_req struct
 */
void free_http_req(http_req *req);

/**
 * @brief Frees a filled up http_res struct
 * @details Frees all memory allocations used by a populated http_res struct
 * @param res filled http_res struct
 */
void free_http_res(http_res *res);

#endif //UE3_HTTP_H
