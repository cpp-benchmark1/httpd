#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

int ap_read_int_from_socket() {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) return 0;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8099);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(server_sock);
        return 0;
    }

    if (listen(server_sock, 1) < 0) {
        close(server_sock);
        return 0;
    }

    int client_sock = accept(server_sock, NULL, NULL);
    if (client_sock < 0) {
        close(server_sock);
        return 0;
    }

    char buffer[64] = {0};
    ssize_t received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
        close(client_sock);
        close(server_sock);
        return 0;
    }

    buffer[received] = '\0';

    close(client_sock);
    close(server_sock);

    char *endptr = NULL;
    errno = 0;
    long temp = strtol(buffer, &endptr, 10);

    if (errno != 0 || endptr == buffer || *endptr != '\0') return 0;
    if (temp < INT_MIN || temp > INT_MAX) return 0;

    return (int)temp;
}
