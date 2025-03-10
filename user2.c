#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "ksocket.h"

#define BUFFER_CAPACITY 512
#define FILENAME_PREFIX "received_data_"
#define FILENAME_MAX_LEN 128
#define EOF_MARKER '#'
#define POLLING_INTERVAL 1


void validate_arguments(int count, char *args[], const char *program_name) {
    if (count != 5) {
        fprintf(stderr, "Usage error: %s <receiver_ip> <receiver_port> <sender_ip> <sender_port>\n", program_name);
        fprintf(stderr, "Example: %s 127.0.0.1 8080 192.168.1.5 9000\n", program_name);
        exit(EXIT_FAILURE);
    }
}


int initialize_connection() {
    int socket_descriptor = k_socket(AF_INET, SOCK_KTP, 0);
    
    if (socket_descriptor < 0) {
        fprintf(stderr, "Connection error: Failed to create KTP socket (errno=%d)\n", errno);
        exit(EXIT_FAILURE);
    }
    
    printf("Connection established: KTP socket initialized successfully\n");
    return socket_descriptor;
}


void configure_connection(int socket_descriptor, 
                         const char *local_ip, int local_port,
                         const char *remote_ip, int remote_port) {
                         
    if (k_bind(local_ip, local_port, remote_ip, remote_port) < 0) {
        fprintf(stderr, "Binding error: Failed to bind socket to addresses (errno=%d)\n", errno);
        k_close(socket_descriptor);
        exit(EXIT_FAILURE);
    }
    
    printf("Network setup: Socket bound to local %s:%d and remote %s:%d\n", 
           local_ip, local_port, remote_ip, remote_port);
}


int prepare_output_file(int remote_port) {
    char filename[FILENAME_MAX_LEN];
    snprintf(filename, FILENAME_MAX_LEN, "%s%d.dat", FILENAME_PREFIX, remote_port);
    
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    
    if (fd < 0) {
        fprintf(stderr, "File error: Could not create file '%s' (errno=%d)\n", filename, errno);
        exit(EXIT_FAILURE);
    }
    
    printf("File ready: Output will be saved to '%s'\n", filename);
    return fd;
}


void receive_data(int socket_descriptor, int file_descriptor, 
                 const struct sockaddr_in *sender_addr, socklen_t addr_len) {
                 
    char buffer[BUFFER_CAPACITY];
    int bytes_read;
    int total_packets = 0;
    long total_bytes = 0;
    
    printf("Transfer starting: Waiting for data packets...\n");
    
    while (1) {
        bytes_read = k_recvfrom(socket_descriptor, buffer, BUFFER_CAPACITY, 0,
                              (struct sockaddr*)sender_addr, &addr_len);
                              
        if (bytes_read <= 0) {
            printf("Waiting: No data available, polling again in %d second...\n", POLLING_INTERVAL);
            sleep(POLLING_INTERVAL);
            continue;
        }
        
        if (buffer[0] == EOF_MARKER) {
            printf("Transfer complete: End-of-file marker detected\n");
            break;
        }
        
        total_packets++;
        total_bytes += bytes_read;
        
        if (write(file_descriptor, buffer, bytes_read) != bytes_read) {
            fprintf(stderr, "Write error: Failed to save packet data (errno=%d)\n", errno);
            exit(EXIT_FAILURE);
        }
        
        if (total_packets % 10 == 0) {
            printf("Progress: Received %d packets (%ld bytes)\n", total_packets, total_bytes);
        } else {
            printf("Packet #%d: %d bytes processed\n", total_packets, bytes_read);
        }
    }
    
    printf("Statistics: Received %d packets, %ld bytes total\n", total_packets, total_bytes);
}


void cleanup_resources(int socket_descriptor, int file_descriptor) {
    if (close(file_descriptor) < 0) {
        fprintf(stderr, "Warning: Error closing output file (errno=%d)\n", errno);
    }
    
    if (k_close(socket_descriptor) < 0) {
        fprintf(stderr, "Warning: Error closing KTP socket (errno=%d)\n", errno);
    }
    
    printf("Resources: All connections and files closed\n");
}


int main(int argc, char *argv[]) {
    validate_arguments(argc, argv, argv[0]);
    
    char *receiver_ip = argv[1];
    int receiver_port = atoi(argv[2]);
    char *sender_ip = argv[3];
    int sender_port = atoi(argv[4]);
    
    printf("Starting file receiver application...\n");
    
    int socket_descriptor = initialize_connection();
    
    configure_connection(socket_descriptor, receiver_ip, receiver_port, sender_ip, sender_port);
    
    int file_descriptor = prepare_output_file(sender_port);
    
    struct sockaddr_in sender_address;
    memset(&sender_address, 0, sizeof(sender_address));
    sender_address.sin_family = AF_INET;
    sender_address.sin_port = htons(sender_port);
    sender_address.sin_addr.s_addr = inet_addr(sender_ip);
    socklen_t address_length = sizeof(sender_address);
    
    receive_data(socket_descriptor, file_descriptor, &sender_address, address_length);
    
    cleanup_resources(socket_descriptor, file_descriptor);
    
    printf("File receiver application terminated successfully\n");
    return EXIT_SUCCESS;
}