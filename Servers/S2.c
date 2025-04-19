#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/wait.h>

#define PORT 7041  
#define BUFFER_SIZE 4096
#define DEBUG 1
#define BASE_DIR_NAME "S2"  
char base_dir[256];

#define debug_print(fmt, ...) \
    do { if (DEBUG) fprintf(stderr, "[S2 DEBUG] %s:%d:%s(): " fmt, __FILE__, \
            __LINE__, __func__, ##__VA_ARGS__); } while (0)

#define handle_error(en, msg) \
    do { debug_print("ERROR: %s - %s\n", msg, strerror(en)); exit(EXIT_FAILURE); } while (0)

void create_directory(const char *path) {
    char cmd[BUFFER_SIZE];
    snprintf(cmd, BUFFER_SIZE, "mkdir -p \"%s/%s\"", base_dir, path);
    debug_print("Creating directory: %s\n", cmd);
    system(cmd);
}

void handle_store(int client_sock, const char *rel_dir_path, const char *file_name) {
    // Build full directory path: ~/S2/<relative_path>
    char full_dir[BUFFER_SIZE];
    snprintf(full_dir, BUFFER_SIZE, "%s/%s", base_dir, rel_dir_path);
    debug_print("Creating full directory: %s\n", full_dir);

    // Create directory recursively using system call (or you can use mkdir_recursive)
    char mkdir_cmd[BUFFER_SIZE];
    snprintf(mkdir_cmd, BUFFER_SIZE, "mkdir -p \"%s\"", full_dir);
    system(mkdir_cmd);

    // Send READY acknowledgment to S1
    const char *ack = "READY";
    send(client_sock, ack, strlen(ack), 0);

    // Construct full file path to save the incoming file
    char full_path[BUFFER_SIZE];
    snprintf(full_path, BUFFER_SIZE, "%s/%s", full_dir, file_name);
    debug_print("Saving PDF to: %s\n", full_path);

    FILE *file = fopen(full_path, "wb");
    if (!file) handle_error(errno, "PDF file creation failed");

    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    int eof_marker = 0;

    // Receive file content from S1
    while ((bytes_received = recv(client_sock, buffer, BUFFER_SIZE, 0)) > 0) {
        if (strstr(buffer, "EOF_FILE_TRANSFER")) {
            eof_marker = 1;
            bytes_received -= 17;  // Trim off EOF marker
        }
        if (bytes_received > 0) {
            fwrite(buffer, 1, bytes_received, file);
        }
        if (eof_marker) break;
    }

    fclose(file);
    debug_print("Stored PDF file successfully: %s\n", full_path);

    // Send confirmation to S1
    const char *confirm = "STORAGE_SUCCESS";
    send(client_sock, confirm, strlen(confirm), 0);
}


void handle_retrieve(int client_sock, const char *path) {
    char full_path[BUFFER_SIZE];
    snprintf(full_path, BUFFER_SIZE, "%s/%s", base_dir, path);
    FILE *file = fopen(full_path, "rb");
    if (!file) {
        send(client_sock, "ERROR: PDF not found", 22, 0);
        return;
    }

    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        send(client_sock, buffer, bytes_read, 0);
    }
    send(client_sock, "EOF_FILE_TRANSFER", 17, 0);
    fclose(file);
}

void handle_delete(int client_sock, const char *path) {
    char full_path[BUFFER_SIZE];
    snprintf(full_path, BUFFER_SIZE, "%s/%s", base_dir, path);
    if (remove(full_path)) {
        send(client_sock, "ERROR: PDF delete failed", 25, 0);
    } else {
        send(client_sock, "DELETE_SUCCESS", 14, 0);
    }
}

void handle_list(int client_sock, const char *path) {
    char full_path[BUFFER_SIZE];
    snprintf(full_path, BUFFER_SIZE, "%s/%s", base_dir, path);
    DIR *dir = opendir(full_path);
    if (!dir) {
        send(client_sock, "ERROR: Directory not found", 26, 0);
        return;
    }

    struct dirent *entry;
    char list[BUFFER_SIZE * 10] = "";
    while ((entry = readdir(dir))) {
        if (entry->d_type == DT_REG) {
            strcat(list, entry->d_name);
            strcat(list, "\n");
        }
    }
    closedir(dir);
    send(client_sock, list, strlen(list), 0);
}

void handle_sendtar(int client_sock, const char *filetype) {
    if (strcmp(filetype, ".pdf") != 0) {
        send(client_sock, "ERROR: Unsupported filetype for tar", 35, 0);
        return;
    }

    char tar_cmd[BUFFER_SIZE];
    snprintf(tar_cmd, sizeof(tar_cmd),
             "find %s -type f -name '*.pdf' | tar -cf /tmp/pdf.tar -T -", base_dir);
    int status = system(tar_cmd);
    if (status != 0) {
        send(client_sock, "ERROR: Failed to create tar", 28, 0);
        return;
    }

    FILE *tarfile = fopen("/tmp/pdf.tar", "rb");
    if (!tarfile) {
        send(client_sock, "ERROR: Tar file not found", 25, 0);
        return;
    }

    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, tarfile)) > 0) {
        send(client_sock, buffer, bytes_read, 0);
    }
    send(client_sock, "EOF_FILE_TRANSFER", 17, 0);
    fclose(tarfile);
}

void process_client(int client_sock) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_read <= 0) {
        debug_print("Connection error: %s\n", strerror(errno));
        close(client_sock);
        return;
    }
    buffer[bytes_read] = '\0';

    debug_print("Command received: %s\n", buffer);

    char cmd[20], arg1[BUFFER_SIZE], arg2[BUFFER_SIZE];
    int args_parsed = sscanf(buffer, "%s %s %s", cmd, arg1, arg2);

    if (strcmp(cmd, "STORE") == 0 && args_parsed == 3) {
        handle_store(client_sock, arg1, arg2);
    } else if (strcmp(cmd, "RETRIEVE") == 0 && args_parsed >= 2) {
        handle_retrieve(client_sock, arg1);
    } else if (strcmp(cmd, "DELETE") == 0 && args_parsed >= 2) {
        handle_delete(client_sock, arg1);
    } else if (strcmp(cmd, "LIST") == 0 && args_parsed >= 2) {
        handle_list(client_sock, arg1);
    } else if (strcmp(cmd, "SENDTAR") == 0 && args_parsed >= 2) {
        handle_sendtar(client_sock, arg1);
    } else {
        const char *err = "ERROR: Invalid command";
        send(client_sock, err, strlen(err), 0);
    }

    close(client_sock);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    snprintf(base_dir, sizeof(base_dir), "%s/%s", getenv("HOME"), BASE_DIR_NAME);
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
        handle_error(errno, "Socket creation failed");

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
        handle_error(errno, "Setsockopt failed");

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
        handle_error(errno, "Bind failed");

    if (listen(server_fd, 5) < 0)
        handle_error(errno, "Listen failed");

    debug_print("S2 PDF Server listening on port %d\n", PORT);
    create_directory("");

    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0) {
            debug_print("Accept error: %s\n", strerror(errno));
            continue;
        }

        debug_print("New connection from %s:%d\n",
                    inet_ntoa(address.sin_addr), ntohs(address.sin_port));

        pid_t pid = fork();
        if (pid < 0) {
            debug_print("Fork failed: %s\n", strerror(errno));
            close(new_socket);
        } else if (pid == 0) {
            close(server_fd);
            process_client(new_socket);
            exit(EXIT_SUCCESS);
        } else {
            close(new_socket);
            waitpid(-1, NULL, WNOHANG);
        }
    }

    return 0;
}
