#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include "ksocket.h"

#define BUFFER_SIZE 496
#define RETRY_DELAY 1
#define EOF_MARKER '#'
#define INPUT_FILE "file.txt"
#define COOLDOWN_PERIOD 100


void exit_with_error(const char* message) {
    perror(message);
    exit(1);
}


void parse_arguments(int count, char *args[], 
                    char *src_ip, char *dst_ip,
                    int *src_port, int *dst_port) {
                    
    if (count != 5) {
        fprintf(stderr, "Usage: %s <source_ip> <source_port> <target_ip> <target_port>\n", args[0]);
        exit(1);
    }
    
    strcpy(src_ip, args[1]);
    strcpy(dst_ip, args[3]);
    *src_port = atoi(args[2]);
    *dst_port = atoi(args[4]);
}


int setup_network(const char *src_ip, int src_port, 
                 const char *dst_ip, int dst_port) {
    
    int sock = k_socket(AF_INET, SOCK_KTP, 0);
    if (sock < 0) {
        exit_with_error("Failed to create KTP socket");
    }
    
    if (k_bind(src_ip, src_port, dst_ip, dst_port) < 0) {
        exit_with_error("Failed to configure socket endpoints");
    }
    
    printf("Network configured: %s:%d --> %s:%d\n", 
           src_ip, src_port, dst_ip, dst_port);
           
    return sock;
}


int open_input_file(const char *filename) {
    int fd = open(filename, O_RDONLY);
    
    if (fd < 0) {
        exit_with_error("Cannot access input file");
    }
    
    struct stat file_stats;
    fstat(fd, &file_stats);
    
    printf("File opened: %s (%ld bytes)\n", 
           filename, (long)file_stats.st_size);
           
    return fd;
}

void send_segment(int sock, const void *data, size_t length, 
                 const struct sockaddr *dest, socklen_t dest_len) {
    
    int result;
    int attempts = 0;
    
    do {
        result = k_sendto(sock, data, length, 0, dest, dest_len);
        
        if (result < 0 && errno == ENOBUFS) {
            printf("Buffer full, waiting before retry...\n");
            sleep(RETRY_DELAY);
            attempts++;
        }
    } while (result < 0 && errno == ENOBUFS);
    
    if (result < 0) {
        exit_with_error("Transmission failed");
    }
}


void transfer_file(int sock, int file_fd, 
                  const struct sockaddr *dest, socklen_t dest_len) {
    
    unsigned char buffer[BUFFER_SIZE];
    int bytes_read;
    int segment_count = 0;
    long total_bytes = 0;
    time_t start_time = time(NULL);
    
    printf("Starting file transfer...\n");
    
    while ((bytes_read = read(file_fd, buffer, BUFFER_SIZE)) > 0) {
        segment_count++;
        total_bytes += bytes_read;
        
        printf("Segment #%d: Sending %d bytes... ", segment_count, bytes_read);
        send_segment(sock, buffer, bytes_read, dest, dest_len);
        printf("Transmitted!\n");
    }
    
    buffer[0] = EOF_MARKER;
    
    printf("Sending end marker... ");
    send_segment(sock, buffer, 1, dest, dest_len);
    printf("Transmitted!\n");
    
    time_t end_time = time(NULL);
    int elapsed = (int)difftime(end_time, start_time);
    
    printf("\nTransfer complete:\n");
    printf("- Segments sent: %d\n", segment_count);
    printf("- Bytes transmitted: %ld\n", total_bytes);
    printf("- Duration: %d seconds\n", elapsed);
    
    if (elapsed > 0) {
        printf("- Average speed: %.2f KB/s\n", (total_bytes / 1024.0) / elapsed);
    }
}


int main(int argc, char *argv[]) {
    char source_ip[16], destination_ip[16];
    int source_port, destination_port;
    
    parse_arguments(argc, argv, source_ip, destination_ip, 
                   &source_port, &destination_port);
    
    int socket_fd = setup_network(source_ip, source_port, 
                                 destination_ip, destination_port);
    
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(destination_port);
    dest_addr.sin_addr.s_addr = inet_addr(destination_ip);
    
    int file_fd = open_input_file(INPUT_FILE);
    
    transfer_file(socket_fd, file_fd, 
                 (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    
    close(file_fd);
    
    printf("Waiting for %d seconds to ensure delivery...\n", COOLDOWN_PERIOD);
    sleep(COOLDOWN_PERIOD);
    
    if (k_close(socket_fd) < 0) {
        exit_with_error("Failed to close KTP socket");
    }
    
    printf("Transfer session completed successfully\n");
    return 0;
}