/*
Name : Srinjoy Das
Roll : 22CS30054

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <signal.h>
#include <pthread.h>
#include "ksocket.h"

SHARED_MEM *SM;
NETWORK_SOCKET* net_socket;
struct sembuf pop, vop;
int semid_SM, semid_net_socket;
int shmid_SM, shmid_net_socket;
int semid_init, semid_ktp;

typedef struct network_socket{
  int socket_id;
  char ip_addr[16];
  uint16_t port;
  int err_code;
}NETWORK_SOCKET;

int drop_Message(void);
void *GC(void);
void *R(void);
void *S(void);
void shutdown_handler(int sig);
void initialize_semaphores(void);
void create_ipc_resources(void);
int setup_shared_memory(void);
void initialize_connections(void);
int create_worker_threads(void);


int drop_Message(void) {
    double random_value = (double)rand() / RAND_MAX;
    return random_value < p;
}




void *GC(void) {
    for (;;) {
        sleep(T);
        ACQUIRE_SEM(semid_SM);
        
        for (int i = 0; i < N; i++) {
            if (SM[i].free == 0) {
                if (kill(SM[i].pid, 0) == -1) {
                    SM[i].free = 1;
                    fprintf(stderr, "GC: Freed connection slot %d (process %d terminated)\n", 
                            i, SM[i].pid);
                }
            }
        }
        
        RELEASE_SEM(semid_SM);
    }
    
    return NULL; 
}


void create_ack_packet(char *ack_buffer, int seq_num, int window_size) {
    ack_buffer[0] = '0';
    
    ack_buffer[8] = ((seq_num >> 0) & 1) + '0';
    ack_buffer[7] = ((seq_num >> 1) & 1) + '0';
    ack_buffer[6] = ((seq_num >> 2) & 1) + '0';
    ack_buffer[5] = ((seq_num >> 3) & 1) + '0';
    ack_buffer[4] = ((seq_num >> 4) & 1) + '0';
    ack_buffer[3] = ((seq_num >> 5) & 1) + '0';
    ack_buffer[2] = ((seq_num >> 6) & 1) + '0';
    ack_buffer[1] = ((seq_num >> 7) & 1) + '0';
    
    ack_buffer[12] = ((window_size >> 0) & 1) + '0';
    ack_buffer[11] = ((window_size >> 1) & 1) + '0';
    ack_buffer[10] = ((window_size >> 2) & 1) + '0';
    ack_buffer[9]  = ((window_size >> 3) & 1) + '0';
}


int process_data_packet(int conn_idx, char *packet, int bytes_received) {
    int sequence = 0;
    for (int bit = 1; bit <= 8; bit++) {
        sequence = (sequence << 1) | (packet[bit] - '0');
    }
    
    int data_length = 0;
    for (int bit = 9; bit <= 18; bit++) {
        data_length = (data_length << 1) | (packet[bit] - '0');
    }
    
    if (sequence == SM[conn_idx].rwnd.start) {
        int buffer_index = SM[conn_idx].rwnd.buffer[sequence];
        
        if (buffer_index >= 0) {
            memcpy(SM[conn_idx].recv_buffer[buffer_index], packet + 19, data_length);
            SM[conn_idx].recv_buffer_live[buffer_index] = 1;
            SM[conn_idx].rwnd.length--;
            SM[conn_idx].rcvbufferlen[buffer_index] = data_length;
            
            while (SM[conn_idx].rwnd.buffer[SM[conn_idx].rwnd.start] >= 0 && 
                   SM[conn_idx].recv_buffer_live[SM[conn_idx].rwnd.buffer[SM[conn_idx].rwnd.start]] == 1) {
                SM[conn_idx].rwnd.start = (SM[conn_idx].rwnd.start + 1) % MAX_SEQ_NUM;
            }
        }
        
        return 1;  // Send ACK
    } 
    else if (SM[conn_idx].rwnd.buffer[sequence] >= 0 && 
            SM[conn_idx].recv_buffer_live[SM[conn_idx].rwnd.buffer[sequence]] == 0) {
        int buffer_index = SM[conn_idx].rwnd.buffer[sequence];
        
        memcpy(SM[conn_idx].recv_buffer[buffer_index], packet + 19, data_length);
        SM[conn_idx].recv_buffer_live[buffer_index] = 1;
        SM[conn_idx].rwnd.length--;
        SM[conn_idx].rcvbufferlen[buffer_index] = data_length;
        
        return 1;  // Send ACK
    }
    
    return 0;  // No ACK needed
}


void process_ack_packet(int conn_idx, char *packet) {
    int acked_seq = 0;
    for (int bit = 1; bit <= 8; bit++) {
        acked_seq = (acked_seq << 1) | (packet[bit] - '0');
    }
    
    int remote_window = 0;
    for (int bit = 9; bit <= 12; bit++) {
        remote_window = (remote_window << 1) | (packet[bit] - '0');
    }
    
    if (SM[conn_idx].swnd.buffer[acked_seq] >= 0) {
        int current = SM[conn_idx].swnd.start;
        
        while (current != (acked_seq + 1) % MAX_SEQ_NUM) {
            SM[conn_idx].swnd.buffer[current] = -1;
            SM[conn_idx].last_send_time[current] = -1;
            SM[conn_idx].send_buffer_size++;
            
            current = (current + 1) % MAX_SEQ_NUM;
        }
        
        SM[conn_idx].swnd.start = (acked_seq + 1) % MAX_SEQ_NUM;
    }
    
    SM[conn_idx].swnd.length = remote_window;
}


void *R(void) {
    fd_set active_fd_set;
    FD_ZERO(&active_fd_set);
    int max_fd = 0;
    
    for (;;) {
        fd_set read_fd_set = active_fd_set;
        
        struct timeval timeout;
        timeout.tv_sec = T;
        timeout.tv_usec = 0;
        
        int result = select(max_fd + 1, &read_fd_set, NULL, NULL, &timeout);
        
        if (result < 0) {
            perror("select() error");
            continue;
        }
        
        if (result == 0) {
            FD_ZERO(&active_fd_set);
            ACQUIRE_SEM(semid_SM);
            max_fd = 0;
            
            for (int i = 0; i < N; i++) {
                if (SM[i].free == 0) {
                    int sock_fd = SM[i].udp_sockid;
                    FD_SET(sock_fd, &active_fd_set);
                    
                    if (sock_fd > max_fd) {
                        max_fd = sock_fd;
                    }
                    
                    if (SM[i].non_space == 1 && SM[i].rwnd.length > 0) {
                        SM[i].non_space = 0;
                    }
                    
                    int last_acked = (SM[i].rwnd.start - 1 + MAX_SEQ_NUM) % MAX_SEQ_NUM;
                    
                    struct sockaddr_in client_addr;
                    memset(&client_addr, 0, sizeof(client_addr));
                    client_addr.sin_family = AF_INET;
                    client_addr.sin_port = htons(SM[i].port);
                    client_addr.sin_addr.s_addr = inet_addr(SM[i].ip_addr);
                    
                    char ack_msg[13];
                    create_ack_packet(ack_msg, last_acked, SM[i].rwnd.length);
                    
                    sendto(SM[i].udp_sockid, ack_msg, 13, 0, 
                           (struct sockaddr*)&client_addr, sizeof(client_addr));
                }
            }
            RELEASE_SEM(semid_SM);
            continue;
        }
        
        if (result > 0) {
            ACQUIRE_SEM(semid_SM);
            
            for (int i = 0; i < N; i++) {
                if (SM[i].free == 0 && FD_ISSET(SM[i].udp_sockid, &read_fd_set)) {
                    char packet_buffer[531];
                    
                    struct sockaddr_in src_addr;
                    socklen_t addr_len = sizeof(src_addr);
                    
                    int bytes_received = recvfrom(SM[i].udp_sockid, packet_buffer, 531, 0,
                                                 (struct sockaddr*)&src_addr, &addr_len);
                    
                    if (drop_Message()) {
                        continue;
                    }
                    
                    if (bytes_received < 0) {
                        perror("recvfrom() error");
                    } else {
                        if (packet_buffer[0] == '1') {
                            // DATA packet
                            if (process_data_packet(i, packet_buffer, bytes_received)) {
                                // Update flow control
                                if (SM[i].rwnd.length == 0) {
                                    SM[i].non_space = 1;
                                }
                                
                                // Send ACK
                                int last_acked = (SM[i].rwnd.start - 1 + MAX_SEQ_NUM) % MAX_SEQ_NUM;
                                char ack_msg[13];
                                
                                create_ack_packet(ack_msg, last_acked, SM[i].rwnd.length);
                                
                                sendto(SM[i].udp_sockid, ack_msg, 13, 0, 
                                       (struct sockaddr*)&src_addr, sizeof(src_addr));
                            }
                        } 
                        else {
                            // ACK packet
                            process_ack_packet(i, packet_buffer);
                        }
                    }
                }
            }
            RELEASE_SEM(semid_SM);
        }
    }
    
    return NULL; // Never reached
}


void create_data_packet(char *packet, int seq_num, char *data, int data_length) {
    // Set packet type to DATA
    packet[0] = '1';
    
    packet[8] = ((seq_num >> 0) & 1) + '0';
    packet[7] = ((seq_num >> 1) & 1) + '0';
    packet[6] = ((seq_num >> 2) & 1) + '0';
    packet[5] = ((seq_num >> 3) & 1) + '0';
    packet[4] = ((seq_num >> 4) & 1) + '0';
    packet[3] = ((seq_num >> 5) & 1) + '0';
    packet[2] = ((seq_num >> 6) & 1) + '0';
    packet[1] = ((seq_num >> 7) & 1) + '0';
    
    packet[18] = ((data_length >> 0) & 1) + '0';
    packet[17] = ((data_length >> 1) & 1) + '0';
    packet[16] = ((data_length >> 2) & 1) + '0';
    packet[15] = ((data_length >> 3) & 1) + '0';
    packet[14] = ((data_length >> 4) & 1) + '0';
    packet[13] = ((data_length >> 5) & 1) + '0';
    packet[12] = ((data_length >> 6) & 1) + '0';
    packet[11] = ((data_length >> 7) & 1) + '0';
    packet[10] = ((data_length >> 8) & 1) + '0';
    packet[9]  = ((data_length >> 9) & 1) + '0';
    
    if (data != NULL) {
        memcpy(packet + 19, data, data_length);
    }
}

void *S(void) {
    for (;;) {
        sleep(T / 2);
        ACQUIRE_SEM(semid_SM);
        
        for (int i = 0; i < N; i++) {
            if (SM[i].free == 0) {
                struct sockaddr_in dest_addr;
                memset(&dest_addr, 0, sizeof(dest_addr));
                dest_addr.sin_family = AF_INET;
                dest_addr.sin_port = htons(SM[i].port);
                dest_addr.sin_addr.s_addr = inet_addr(SM[i].ip_addr);
                
                int timeout_occurred = 0;
                int current_seq = SM[i].swnd.start;
                int end_seq = (SM[i].swnd.start + SM[i].swnd.length) % MAX_SEQ_NUM;
                
                while (current_seq != end_seq) {
                    time_t current_time = time(NULL);
                    
                    if (SM[i].last_send_time[current_seq] != -1 && 
                        current_time - SM[i].last_send_time[current_seq] > T) {
                        timeout_occurred = 1;
                        break;
                    }
                    
                    current_seq = (current_seq + 1) % MAX_SEQ_NUM;
                }
                
                                if (timeout_occurred) {
                                  fprintf(stderr, "Timeout Detected on connection %d!\n", i);
                                  
                                  current_seq = SM[i].swnd.start;
                                  
                                  while (current_seq != end_seq) {
                                      if (SM[i].swnd.buffer[current_seq] != -1) {
                                          char packet_buffer[531];
                                          int buffer_idx = SM[i].swnd.buffer[current_seq];
                                          int data_length = SM[i].sendbufferlen[buffer_idx];
                                          
                                          create_data_packet(packet_buffer, current_seq, 
                                                            SM[i].send_buffer[buffer_idx], 
                                                            data_length);
                                          
                                          sendto(SM[i].udp_sockid, packet_buffer, data_length + 19, 0,
                                                (struct sockaddr*)&dest_addr, sizeof(dest_addr));
                                                
                                          SM[i].last_send_time[current_seq] = time(NULL);
                                      }
                                      
                                      current_seq = (current_seq + 1) % MAX_SEQ_NUM;
                                  }
                              } 
                              else {
                                  current_seq = SM[i].swnd.start;
                                  
                                  while (current_seq != end_seq) {
                                      if (SM[i].swnd.buffer[current_seq] != -1 && SM[i].last_send_time[current_seq] == -1) {
                                          char packet_buffer[531];
                                          int buffer_idx = SM[i].swnd.buffer[current_seq];
                                          int data_length = SM[i].sendbufferlen[buffer_idx];
                                          
                                          create_data_packet(packet_buffer, current_seq, 
                                                            SM[i].send_buffer[buffer_idx], 
                                                            data_length);
                                          
                                          sendto(SM[i].udp_sockid, packet_buffer, data_length + 19, 0,
                                                (struct sockaddr*)&dest_addr, sizeof(dest_addr));
                                                
                                          SM[i].last_send_time[current_seq] = time(NULL);
                                      }
                                      
                                      current_seq = (current_seq + 1) % MAX_SEQ_NUM;
                                  }
                              }
                          }
                      }
                      
                      RELEASE_SEM(semid_SM);
                  }
                  
                  return NULL; // Never reached
              }
              

              void shutdown_handler(int sig) {
                  if (sig == SIGINT) {
                      fprintf(stderr, "\nInitiating server shutdown...\n");
                      
                      shmdt(net_socket);
                      shmdt(SM);
                      
                      shmctl(shmid_net_socket, IPC_RMID, NULL);
                      shmctl(shmid_SM, IPC_RMID, NULL);
                      
                      semctl(semid_net_socket, 0, IPC_RMID);
                      semctl(semid_SM, 0, IPC_RMID);
                      semctl(semid_init, 0, IPC_RMID);
                      semctl(semid_ktp, 0, IPC_RMID);
                      
                      printf("Server shutdown complete. All resources freed.\n");
                      exit(0);
                  }
              }
              

              void initialize_semaphores(void) {
                  pop.sem_num = 0;
                  pop.sem_op = -1;
                  pop.sem_flg = 0;
              
                  vop.sem_num = 0;
                  vop.sem_op = 1;
                  vop.sem_flg = 0;
                  
                  printf("Semaphore operations initialized\n");
              }
              

              void create_ipc_resources(void) {
                  // Generate IPC keys
                  key_t key_shmid_net_socket = ftok("/", 'A');
                  key_t key_semid_net_socket = ftok("/", 'B');
                  key_t key_shmid_SM = ftok("/", 'C');
                  key_t key_semid_SM = ftok("/", 'D');
                  key_t key_semid_init = ftok("/", 'E');
                  key_t key_semid_ktp = ftok("/", 'F');
                  
                  if (key_shmid_net_socket == -1 || key_semid_net_socket == -1 || 
                      key_shmid_SM == -1 || key_semid_SM == -1 || 
                      key_semid_init == -1 || key_semid_ktp == -1) {
                      perror("Failed to generate IPC keys");
                      exit(1);
                  }
                  
                  shmid_net_socket = shmget(key_shmid_net_socket, sizeof(NETWORK_SOCKET), 0666 | IPC_CREAT);
                  shmid_SM = shmget(key_shmid_SM, sizeof(SHARED_MEM) * N, 0666 | IPC_CREAT);
                  
                  if (shmid_net_socket == -1 || shmid_SM == -1) {
                      perror("Failed to create shared memory");
                      exit(1);
                  }
                  
                  semid_net_socket = semget(key_semid_net_socket, 1, 0666 | IPC_CREAT);
                  semid_SM = semget(key_semid_SM, 1, 0666 | IPC_CREAT);
                  semid_init = semget(key_semid_init, 1, 0666 | IPC_CREAT);
                  semid_ktp = semget(key_semid_ktp, 1, 0666 | IPC_CREAT);
                  
                  if (semid_net_socket == -1 || semid_SM == -1 || semid_init == -1 || semid_ktp == -1) {
                      perror("Failed to create semaphores");
                      exit(1);
                  }
                  
                  semctl(semid_net_socket, 0, SETVAL, 1);
                  semctl(semid_SM, 0, SETVAL, 1);
                  semctl(semid_init, 0, SETVAL, 0);
                  semctl(semid_ktp, 0, SETVAL, 0);
                  
                  printf("IPC resources created successfully\n");
              }
              

              int setup_shared_memory(void) {
                  net_socket = (NETWORK_SOCKET*)shmat(shmid_net_socket, NULL, 0);
                  SM = (SHARED_MEM*)shmat(shmid_SM, NULL, 0);
                  
                  if (net_socket == (void*)-1 || SM == (void*)-1) {
                      perror("Failed to attach to shared memory");
                      return -1;
                  }
                  
                  memset(net_socket, 0, sizeof(NETWORK_SOCKET));
                  
                  printf("Shared memory attached and initialized\n");
                  return 0;
              }

              void initialize_connections(void) {
                  ACQUIRE_SEM(semid_SM);
                  for (int i = 0; i < N; i++) {
                      SM[i].free = 1; 
                      
                      SM[i].pid = 0;
                      SM[i].udp_sockid = -1;
                      SM[i].send_buffer_size = 10;
                      SM[i].swnd.length = 0;
                      SM[i].rwnd.length = 0;
                      SM[i].swnd.start = 0;
                      SM[i].rwnd.start = 0;
                      SM[i].non_space = 0;
                  }
                  RELEASE_SEM(semid_SM);
                  
                  printf("Connection slots initialized (capacity: %d)\n", N);
              }
              

              int create_worker_threads(void) {
                  pthread_t receiver_thread, sender_thread, gc_thread;
                  pthread_attr_t thread_attr;
                  
                  if (pthread_attr_init(&thread_attr) != 0) {
                      perror("Failed to initialize thread attributes");
                      return -1;
                  }
                  
                  pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
                  
                  if (pthread_create(&receiver_thread, &thread_attr, R, NULL) != 0) {
                      perror("Failed to create receiver thread");
                      pthread_attr_destroy(&thread_attr);
                      return -1;
                  }
                  printf("Receiver thread created\n");
                  
                  if (pthread_create(&sender_thread, &thread_attr, S, NULL) != 0) {
                      perror("Failed to create sender thread");
                      pthread_attr_destroy(&thread_attr);
                      return -1;
                  }
                  printf("Sender thread created\n");
                  
                  if (pthread_create(&gc_thread, &thread_attr, GC, NULL) != 0) {
                      perror("Failed to create garbage collector thread");
                      pthread_attr_destroy(&thread_attr);
                      return -1;
                  }
                  printf("Garbage collector created\n");
                  
                  pthread_attr_destroy(&thread_attr);
                  
                  return 0;
              }
              
int main() {
    printf("// Reliable UDP Service - " __DATE__ " " __TIME__ "\n");

                  
    signal(SIGINT, shutdown_handler);
                  
    srand(time(NULL));
                  
    initialize_semaphores();
                  
    create_ipc_resources();
                  
    if (setup_shared_memory() != 0) {
        fprintf(stderr, "Failed to set up shared memory\n");
        exit(1);
    }
                  
    initialize_connections();
                  
    if (create_worker_threads() != 0) {
        fprintf(stderr, "Failed to create worker threads\n");
        exit(1);
    }

                  
    for (;;) {
        ACQUIRE_SEM(semid_init);
        ACQUIRE_SEM(semid_net_socket);
        
        if (net_socket->socket_id == 0 && net_socket->port == 0) {
            int new_socket = socket(AF_INET, SOCK_DGRAM, 0);
            if (new_socket < 0) {
                net_socket->err_code = errno;
                net_socket->socket_id = -1;
            } else {
                net_socket->socket_id = new_socket;
            }
        } 
        else {
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(net_socket->port);
            addr.sin_addr.s_addr = inet_addr(net_socket->ip_addr);
            
            if (bind(net_socket->socket_id, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
                net_socket->err_code = errno;
                net_socket->socket_id = -1;
            }
        }
        
        RELEASE_SEM(semid_net_socket);
        RELEASE_SEM(semid_ktp);
    }
                  
    return 0;  
}