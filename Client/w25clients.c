#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#define S1_PORT 7040
#define BUFFER_SIZE 4096
#define DEBUG 1

// Debug printing macro
#define debug_print(fmt, ...) \
    do { if (DEBUG) fprintf(stderr, "[DEBUG] %s:%d:%s(): " fmt, __FILE__, \
            __LINE__, __func__, __VA_ARGS__); } while (0)

// Error handling macro
#define handle_error(en, msg) \
    do { debug_print("ERROR: %s - %s\n", msg, strerror(en)); exit(EXIT_FAILURE); } while (0)

int sock = 0;

void connect_to_server() {
    struct sockaddr_in serv_addr;
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        handle_error(errno, "Socket creation error");
    }
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(S1_PORT);
    
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        handle_error(errno, "Invalid address/Address not supported");
    }
    
    debug_print("Attempting connection to S1 at 127.0.0.1:%d\n", S1_PORT);
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        handle_error(errno, "Connection Failed");
    }
    debug_print("Connected to S1 successfully\n", 0);
}

void send_command(const char* command) {
    debug_print("Sending command: %s\n", command);
    if (send(sock, command, strlen(command), 0) < 0) {
        handle_error(errno, "Send failed");
    }
}

void receive_response(char* buffer, size_t size) {
    memset(buffer, 0, size);
    ssize_t bytes_received = recv(sock, buffer, size - 1, 0);
    if (bytes_received < 0) {
        handle_error(errno, "Receive failed");
    }
    debug_print("Received response: %s\n", buffer);
}

void handle_upload(const char* filename, const char* dest_path) {
    struct stat st;
    if (stat(filename, &st)) {
        fprintf(stderr, "Error: File '%s' not found\n", filename);
        return;
    }
    
    
    const char *ext = strrchr(filename, '.');
    if (!ext || (strcmp(ext, ".c") != 0 && strcmp(ext, ".pdf") != 0 &&
             strcmp(ext, ".txt") != 0 && strcmp(ext, ".zip") != 0)) {
    fprintf(stderr, "Error: Invalid file extension\n");
    return;
}


    char command[BUFFER_SIZE];
    snprintf(command, BUFFER_SIZE, "uploadf %s %s", filename, dest_path);
    send_command(command);

    // Sending file content
    FILE* file = fopen(filename, "rb");
    if (!file) {
        handle_error(errno, "File open failed");
    }

    char file_buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(file_buffer, 1, BUFFER_SIZE, file)) > 0) {
        if (send(sock, file_buffer, bytes_read, 0) < 0) {
            handle_error(errno, "File send failed");
        }
    }
    fclose(file);
    
    // Sending end of file marker
    char eof_marker[] = "EOF_FILE_TRANSFER";
    send(sock, eof_marker, strlen(eof_marker), 0);
    
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);
    printf("Upload result: %s\n", response);
}

void handle_download(const char* remote_path) {
    char command[BUFFER_SIZE];
    snprintf(command, BUFFER_SIZE, "downlf %s", remote_path);
    send_command(command);

    const char* filename = strrchr(remote_path, '/');
    filename = filename ? filename + 1 : remote_path;

    mkdir("downloads", 0777);  // ensure folder exists
    char full_path[BUFFER_SIZE];
    snprintf(full_path, BUFFER_SIZE, "downloads/%s", filename);

    FILE* file = fopen(full_path, "wb");
    if (!file) {
        handle_error(errno, "File creation failed");
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    int eof_received = 0;

    while ((bytes_received = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
        char* eof_marker = strstr(buffer, "EOF_FILE_TRANSFER");
        if (eof_marker) {
            size_t valid_bytes = eof_marker - buffer;
            if (valid_bytes > 0) {
                fwrite(buffer, 1, valid_bytes, file);
            }
            eof_received = 1;
            break;
        }
        fwrite(buffer, 1, bytes_received, file);
    }

    fclose(file);

    if (eof_received) {
        printf("File downloaded successfully as %s\n", full_path);
    } else {
        printf("Download failed: Did not receive EOF marker.\n");
    }
}

void handle_remove(const char* remote_path) {
    char command[BUFFER_SIZE];
    snprintf(command, BUFFER_SIZE, "removef %s", remote_path);
    send_command(command);
    
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);
    printf("Remove operation result: %s\n", response);
}

void handle_downltar(const char* filetype) {
    char command[BUFFER_SIZE];
    snprintf(command, BUFFER_SIZE, "downltar %s", filetype);
    send_command(command);
    
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);
    
    if (strncmp(response, "ERROR", 5) == 0) {
        printf("Tar creation failed: %s\n", response);
        return;
    }

    char tar_filename[BUFFER_SIZE];
    snprintf(tar_filename, BUFFER_SIZE, "%sfiles.tar", filetype);
    
    FILE* file = fopen(tar_filename, "wb");
    if (!file) {
        handle_error(errno, "Tar file creation failed");
    }

    char file_buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    while (bytes_received = recv(sock, file_buffer, BUFFER_SIZE, 0)) {
        if (strstr(file_buffer, "EOF_FILE_TRANSFER")) break;
        fwrite(file_buffer, 1, bytes_received, file);
    }
    
    fclose(file);
    printf("Tar archive downloaded as %s\n", tar_filename);
}

void handle_dispfnames(const char* pathname) {
    char command[BUFFER_SIZE];
    snprintf(command, BUFFER_SIZE, "dispfnames %s", pathname);
    send_command(command);

    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);
    
    if (strncmp(response, "ERROR", 5) == 0) {
        printf("Directory listing failed: %s\n", response);
        return;
    }

    printf("Files in %s:\n%s\n", pathname, response);
}

int main() {
    connect_to_server();

    while(1) {
        printf("w25client$ ");
        char input[BUFFER_SIZE];
        if (!fgets(input, BUFFER_SIZE, stdin)) break;

        // Removing newline and parse command
        input[strcspn(input, "\n")] = 0;
        char *cmd = strtok(input, " ");
        
        if (!cmd) continue;

        if (strcmp(cmd, "uploadf") == 0) {
            char *filename = strtok(NULL, " ");
            char *dest_path = strtok(NULL, " ");
            if (!filename || !dest_path) {
                fprintf(stderr, "Invalid syntax. Usage: uploadf filename destination_path\n");
                continue;
            }
            handle_upload(filename, dest_path);
        }
        else if (strcmp(cmd, "downlf") == 0) {
            char *remote_path = strtok(NULL, " ");
            if (!remote_path) {
                fprintf(stderr, "Invalid syntax. Usage: downlf remote_path\n");
                continue;
            }
            handle_download(remote_path);
        }
        else if (strcmp(cmd, "removef") == 0) {
            char *remote_path = strtok(NULL, " ");
            if (!remote_path) {
                fprintf(stderr, "Invalid syntax. Usage: removef remote_path\n");
                continue;
            }
            handle_remove(remote_path);
        }
        else if (strcmp(cmd, "downltar") == 0) {
            char *filetype = strtok(NULL, " ");
            if (!filetype) {
                fprintf(stderr, "Invalid syntax. Usage: downltar filetype\n");
                continue;
            }
            handle_downltar(filetype);
        }
        else if (strcmp(cmd, "dispfnames") == 0) {
            char *pathname = strtok(NULL, " ");
            if (!pathname) {
                fprintf(stderr, "Invalid syntax. Usage: dispfnames pathname\n");
                continue;
            }
            handle_dispfnames(pathname);
        }
        else if (strcmp(cmd, "exit") == 0) {
            break;
        }
        else {
            fprintf(stderr, "Invalid command. Available commands:\n");
            fprintf(stderr, "uploadf, downlf, removef, downltar, dispfnames, exit\n");
        }
    }

    close(sock);
    return 0;
}