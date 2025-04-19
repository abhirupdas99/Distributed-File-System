// All includes and definitions
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <libgen.h>
#include <arpa/inet.h>
#include <errno.h>

#define PORT 7040
#define S2_PORT 7041
#define S3_PORT 7042
#define S4_PORT 7043
#define BUFFER_SIZE 4096
#define DEBUG 1
#define BASE_DIR_NAME "S1"
char base_dir[256];

#define debug_print(fmt, ...) \
    do { if (DEBUG) fprintf(stderr, "[S1 DEBUG] %s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); } while (0)

#define handle_error(en, msg) \
    do { debug_print("ERROR: %s - %s\n", msg, strerror(en)); exit(EXIT_FAILURE); } while (0)

typedef enum {PDF, TXT, ZIP, C_FILE} file_type;

// Function declarations
void prcclient(int client_sock);
file_type get_file_type(const char *filename);
void create_directory(const char *path);
void forward_file(const char *filename, const char *dest_path, file_type type);
void handle_downlf(int client_sock, const char *filepath);
void handle_removef(int client_sock, const char *filepath);
void handle_downltar(int client_sock, const char *filetype);
void handle_dispfnames(int client_sock, const char *dirpath);

// Utility functions
char* expand_path(const char* path) {
    if (path[0] == '~') {
        const char* home = getenv("HOME");
        if (!home) home = "/home/unknown";
        char* expanded = malloc(strlen(home) + strlen(path));
        if (!expanded) { perror("malloc failed"); exit(EXIT_FAILURE); }
        sprintf(expanded, "%s%s", home, path + 1);
        return expanded;
    }
    return strdup(path);
}

// void create_directory(const char *path) {
//     struct stat st;
//     if (stat(path, &st) == 0) return;
//     if (mkdir(path, 0777) == -1 && errno != EEXIST) handle_error(errno, "mkdir failed");
// }
void create_directory(const char *path) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path);

    char *p = tmp;
    while (*p == '/') p++;  // Skip leading slashes

    for (; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

file_type get_file_type(const char *filename){
    const char *ext = strrchr(filename, '.');
    if (!ext) return C_FILE;
    if (strcmp(ext, ".pdf") == 0) return PDF;
    if (strcmp(ext, ".txt") == 0) return TXT;
    if (strcmp(ext, ".zip") == 0) return ZIP;
    return C_FILE;
}

void send_command_to_storage(int sock, const char *command) {
    send(sock, command, strlen(command), 0);
    char ack[10]; recv(sock, ack, sizeof(ack), 0);
}

void forward_file(const char *filename, const char *dest_path, file_type type){
    int port = (type == PDF) ? S2_PORT : (type == TXT) ? S3_PORT : S4_PORT;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr = { .sin_family = AF_INET, .sin_port = htons(port) };
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);
    connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

    char *dc1 = expand_path(dest_path), *dc2 = expand_path(dest_path);
    char *file_part = basename(dc1), *dir_part = dirname(dc2);
    char command[BUFFER_SIZE];
    snprintf(command, BUFFER_SIZE, "STORE %s %s", dir_part, file_part);
    send_command_to_storage(sock, command);

    FILE *file = fopen(filename, "rb");
    char buffer[BUFFER_SIZE];
    size_t bytes;
    while ((bytes = fread(buffer, 1, BUFFER_SIZE, file)) > 0) send(sock, buffer, bytes, 0);
    send(sock, "EOF_FILE_TRANSFER", 17, 0);

    fclose(file); close(sock); free(dc1); free(dc2);
}

// Command Handlers
void handle_downlf(int client_sock, const char *filepath) {
    if (!filepath) { send(client_sock, "ERROR: Invalid syntax", 22, 0); return; }

    char path[BUFFER_SIZE];
    strncpy(path, filepath, BUFFER_SIZE);
    if (strncmp(path, "~S1/", 4) == 0) memmove(path, path + 4, strlen(path) - 3);

    file_type type = get_file_type(path);
    char full_path[BUFFER_SIZE]; snprintf(full_path, BUFFER_SIZE, "%s/%s", base_dir, path);

    if (type == C_FILE) {
        FILE *file = fopen(full_path, "rb");
        if (!file) { send(client_sock, "ERROR: File not found", 22, 0); return; }

        char buffer[BUFFER_SIZE];
        size_t bytes;
        while ((bytes = fread(buffer, 1, BUFFER_SIZE, file)) > 0) send(client_sock, buffer, bytes, 0);
        send(client_sock, "EOF_FILE_TRANSFER", 17, 0);
        fclose(file);
    } else {
        int port = (type == PDF) ? S2_PORT : (type == TXT) ? S3_PORT : S4_PORT;
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons(port) };
        inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
        connect(sock, (struct sockaddr*)&sa, sizeof(sa));

        char command[BUFFER_SIZE]; snprintf(command, BUFFER_SIZE, "RETRIEVE %s", path);
        send(sock, command, strlen(command), 0);

        char buffer[BUFFER_SIZE]; ssize_t r;
        while ((r = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
            send(client_sock, buffer, r, 0);
            if (strstr(buffer, "EOF_FILE_TRANSFER")) break;
        }
        close(sock);
    }
}

void handle_removef(int client_sock, const char *filepath) {
    if (!filepath) { send(client_sock, "ERROR: Invalid syntax", 22, 0); return; }

    char path[BUFFER_SIZE];
    strncpy(path, filepath, BUFFER_SIZE);
    if (strncmp(path, "~S1/", 4) == 0) memmove(path, path + 4, strlen(path) - 3);

    file_type type = get_file_type(path);
    char full_path[BUFFER_SIZE]; snprintf(full_path, BUFFER_SIZE, "%s/%s", base_dir, path);

    if (type == C_FILE) {
        if (remove(full_path) == 0) send(client_sock, "REMOVE_SUCCESS", 14, 0);
        else send(client_sock, "ERROR: Deletion failed", 23, 0);
    } else {
        int port = (type == PDF) ? S2_PORT : (type == TXT) ? S3_PORT : S4_PORT;
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons(port) };
        inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
        connect(sock, (struct sockaddr*)&sa, sizeof(sa));

        char command[BUFFER_SIZE]; snprintf(command, BUFFER_SIZE, "DELETE %s", path);
        send(sock, command, strlen(command), 0);
        char response[64]; recv(sock, response, sizeof(response), 0);
        send(client_sock, response, strlen(response), 0);
        close(sock);
    }
}

void handle_downltar(int client_sock, const char *ftype) {
    if (!ftype || (strcmp(ftype, ".c") && strcmp(ftype, ".pdf") && strcmp(ftype, ".txt"))) {
        send(client_sock, "ERROR: Invalid filetype", 24, 0); return;
    }

    if (strcmp(ftype, ".c") == 0) {
        system("tar -cf /tmp/cfiles.tar -C $HOME/S1 . --exclude='*.pdf' --exclude='*.txt' --exclude='*.zip'");
        FILE *file = fopen("/tmp/cfiles.tar", "rb");
        if (!file) { send(client_sock, "ERROR: Could not create tar", 27, 0); return; }

        char buffer[BUFFER_SIZE]; size_t bytes;
        while ((bytes = fread(buffer, 1, BUFFER_SIZE, file)) > 0) send(client_sock, buffer, bytes, 0);
        send(client_sock, "EOF_FILE_TRANSFER", 17, 0); fclose(file);
    } else {
        int port = (strcmp(ftype, ".pdf") == 0) ? S2_PORT : S3_PORT;
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons(port) };
        inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
        connect(sock, (struct sockaddr*)&sa, sizeof(sa));

        char command[BUFFER_SIZE]; snprintf(command, BUFFER_SIZE, "SENDTAR %s", ftype);
        send(sock, command, strlen(command), 0);

        char buffer[BUFFER_SIZE]; ssize_t r;
        while ((r = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
            send(client_sock, buffer, r, 0);
            if (strstr(buffer, "EOF_FILE_TRANSFER")) break;
        }
        close(sock);
    }
}

void handle_dispfnames(int client_sock, const char *dirpath) {
    if (!dirpath) { send(client_sock, "ERROR: Invalid syntax", 22, 0); return; }

    char path[BUFFER_SIZE]; strncpy(path, dirpath, BUFFER_SIZE);
    if (strncmp(path, "~S1/", 4) == 0) memmove(path, path + 4, strlen(path) - 3);

    char full_path[BUFFER_SIZE]; snprintf(full_path, BUFFER_SIZE, "%s/%s", base_dir, path);
    DIR *dir = opendir(full_path);
    if (!dir) { send(client_sock, "ERROR: Directory not found", 26, 0); return; }

    struct dirent *entry;
    char files[BUFFER_SIZE * 2] = "";
    while ((entry = readdir(dir))) {
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".c")) {
            strcat(files, entry->d_name); strcat(files, "\n");
        }
    }
    closedir(dir);

    int ports[] = { S2_PORT, S3_PORT, S4_PORT };
    for (int i = 0; i < 3; i++) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons(ports[i]) };
        inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
        if (connect(sock, (struct sockaddr*)&sa, sizeof(sa)) == 0) {
            char command[BUFFER_SIZE]; snprintf(command, BUFFER_SIZE, "LIST %s", path);
            send(sock, command, strlen(command), 0);
            char buffer[BUFFER_SIZE]; ssize_t r = recv(sock, buffer, BUFFER_SIZE - 1, 0);
            if (r > 0) { buffer[r] = '\0'; strcat(files, buffer); }
            close(sock);
        }
    }
    send(client_sock, files, strlen(files), 0);
}

void prcclient(int client_sock) {
    char buffer[BUFFER_SIZE];
    while (1) {
        bzero(buffer, BUFFER_SIZE);
        ssize_t bytes_read = read(client_sock, buffer, BUFFER_SIZE);
        if (bytes_read <= 0) {
            debug_print("Client disconnected\n");
            break;
        }

        debug_print("Command: %s\n", buffer);
        char *cmd = strtok(buffer, " ");

        if (strcmp(cmd, "uploadf") == 0) {
            char *filename = strtok(NULL, " ");
            char *dest_path = strtok(NULL, " ");
            if (!filename || !dest_path) {
                send(client_sock, "ERROR: Invalid syntax", 22, 0);
                continue;
            }

            char processed_path[BUFFER_SIZE];
            strncpy(processed_path, dest_path, BUFFER_SIZE);
            if (strncmp(processed_path, "~S1/", 4) == 0)
                memmove(processed_path, processed_path + 4, strlen(processed_path) - 3);

            char *dest_copy1 = strdup(processed_path);
            char *dest_copy2 = strdup(processed_path);
            char *dir_part = dirname(dest_copy1);
            char *file_part = basename(dest_copy2);

            char dir_path[BUFFER_SIZE];
            snprintf(dir_path, BUFFER_SIZE, "%s/%s", base_dir, dir_part);
            create_directory(dir_path);

            char final_path[BUFFER_SIZE];
            snprintf(final_path, BUFFER_SIZE, "%s/%s", dir_path, file_part);

            char temp_path[BUFFER_SIZE];
            snprintf(temp_path, BUFFER_SIZE, "%s.tmp", final_path);
            FILE *file = fopen(temp_path, "wb");
            if (!file) {
                send(client_sock, "ERROR: File creation failed", 28, 0);
                free(dest_copy1); free(dest_copy2);
                continue;
            }

            int eof_marker = 0;
            while (!eof_marker && (bytes_read = read(client_sock, buffer, BUFFER_SIZE)) > 0) {
                char *eof_pos = strstr(buffer, "EOF_FILE_TRANSFER");
                if (eof_pos) {
                    eof_marker = 1;
                    size_t valid_bytes = eof_pos - buffer;
                    if (valid_bytes > 0) fwrite(buffer, 1, valid_bytes, file);
                } else {
                    fwrite(buffer, 1, bytes_read, file);
                }
            }
            fclose(file);

            if (rename(temp_path, final_path) != 0) {
                send(client_sock, "ERROR: Save failed", 20, 0);
                remove(temp_path);
                free(dest_copy1); free(dest_copy2);
                continue;
            }

            file_type type = get_file_type(final_path);
            if (type != C_FILE) {
                forward_file(final_path, processed_path, type);
                remove(final_path);
            }

            send(client_sock, "UPLOAD_SUCCESS", 14, 0);
            free(dest_copy1); free(dest_copy2);
        } 
        else if (strcmp(cmd, "downlf") == 0) {
            char *filepath = strtok(NULL, " ");
            handle_downlf(client_sock, filepath);
        } 
        else if (strcmp(cmd, "removef") == 0) {
            char *filepath = strtok(NULL, " ");
            handle_removef(client_sock, filepath);
        } 
        else if (strcmp(cmd, "downltar") == 0) {
            char *filetype = strtok(NULL, " ");
            handle_downltar(client_sock, filetype);
        } 
        else if (strcmp(cmd, "dispfnames") == 0) {
            char *dirpath = strtok(NULL, " ");
            handle_dispfnames(client_sock, dirpath);
        } 
        else {
            send(client_sock, "ERROR: Unknown command", 22, 0);
        }
    }
    close(client_sock);
    exit(EXIT_SUCCESS);
}


// Main function
int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    snprintf(base_dir, sizeof(base_dir), "%s/%s", getenv("HOME"), BASE_DIR_NAME);
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) handle_error(errno, "Socket failed");
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) handle_error(errno, "Setsockopt failed");

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) handle_error(errno, "Bind failed");
    if (listen(server_fd, 5) < 0) handle_error(errno, "Listen failed");

    debug_print("Server listening on port %d\n", PORT);
    create_directory(base_dir);

    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0) continue;
        pid_t pid = fork();
        if (pid == 0) {
            close(server_fd);
            prcclient(new_socket);
        } else {
            close(new_socket);
            waitpid(-1, NULL, WNOHANG);
        }
    }
    return 0;
}
