/*
Name : Srinjoy Das
Roll : 22CS30054

*/

#include "ksocket.h"

SHARED_MEM* SM;
NETWORK_SOCKET* net_socket;
int semid_SM, semid_net_socket;
int shmid_SM, shmid_net_socket;
int semid_init, semid_ktp;
struct sembuf pop, vop;
typedef struct network_socket{
  int socket_id;
  char ip_addr[16];
  uint16_t port;
  int err_code;
}NETWORK_SOCKET;

#define BUFFER_CAPACITY 10

void clean_net_socket(NETWORK_SOCKET* sock) {
    if (sock) {
        sock->socket_id = 0;
        sock->port = 0;
        sock->err_code = 0;
        sock->ip_addr[0] = '\0';
    }
}

void init_connection_slot(int index) {
    if (index < 0 || index >= N) return;
    
    SM[index].free = 0;
    SM[index].pid = getpid();
    
    for (int i = 0; i < MAX_SEQ_NUM; i++) {
        SM[index].swnd.buffer[i] = -1;
        
        if (i < 10) {
            SM[index].rwnd.buffer[i] = i;  
        } else {
            SM[index].rwnd.buffer[i] = -1; 
        }
        
        SM[index].last_send_time[i] = -1;
    }
    
    SM[index].swnd.length = 10;
    SM[index].rwnd.length = 10;
    SM[index].swnd.start = 0;
    SM[index].rwnd.start = 0;
    SM[index].send_buffer_size = 10;
    
    for (int i = 0; i < 10; i++) {
        SM[index].recv_buffer_live[i] = 0;
    }
    
    SM[index].recv_buffer_index = 0;
    SM[index].non_space = 0;
}

void initialize_resources() {
    key_t socket_shm_key = ftok("/", 'A');
    key_t socket_sem_key = ftok("/", 'B');
    key_t conn_shm_key = ftok("/", 'C');
    key_t conn_sem_key = ftok("/", 'D');
    key_t init_sem_key = ftok("/", 'E');
    key_t ready_sem_key = ftok("/", 'F');
    
    shmid_net_socket = shmget(socket_shm_key, sizeof(NETWORK_SOCKET), 0666);
    semid_net_socket = semget(socket_sem_key, 1, 0666);
    shmid_SM = shmget(conn_shm_key, sizeof(SHARED_MEM) * N, 0666);
    semid_SM = semget(conn_sem_key, 1, 0666);
    semid_init = semget(init_sem_key, 1, 0666);
    semid_ktp = semget(ready_sem_key, 1, 0666);
    
    if (shmid_net_socket < 0 || semid_net_socket < 0 || 
        shmid_SM < 0 || semid_SM < 0 || 
        semid_init < 0 || semid_ktp < 0) {
        fprintf(stderr, "Failed to connect to IPC resources: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    SM = (SHARED_MEM*)shmat(shmid_SM, NULL, 0);
    net_socket = (NETWORK_SOCKET*)shmat(shmid_net_socket, NULL, 0);
    
    if (SM == (void*)-1 || net_socket == (void*)-1) {
        fprintf(stderr, "Failed to attach shared memory: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void setup_semaphores() {
    pop.sem_num = 0;
    pop.sem_op = -1;
    pop.sem_flg = 0;
    
    vop.sem_num = 0;
    vop.sem_op = 1;
    vop.sem_flg = 0;
}

int drop_Message() {
    return ((double)rand() / RAND_MAX) < p ? 1 : 0;
}

int find_connection_slot() {
    for (int i = 0; i < N; i++) {
        if (SM[i].free) {
            return i;
        }
    }
    return -1;
}

int find_process_socket() {
    pid_t current_pid = getpid();
    for (int i = 0; i < N; i++) {
        if (!SM[i].free && SM[i].pid == current_pid) {
            return i;
        }
    }
    return -1;
}

int find_buffer_slot(int sock_idx) {
    if (sock_idx < 0 || sock_idx >= N) return -1;
    
    for (int buf = 0; buf < BUFFER_CAPACITY; buf++) {
        int used = 0;
        for (int seq = 0; seq < MAX_SEQ_NUM; seq++) {
            if (SM[sock_idx].swnd.buffer[seq] == buf) {
                used = 1;
                break;
            }
        }
        if (!used) return buf;
    }
    return -1;
}

int find_next_sequence(int sock_idx) {
    if (sock_idx < 0 || sock_idx >= N) return -1;
    
    int start = SM[sock_idx].swnd.start;
    int seq = start;
    
    do {
        if (SM[sock_idx].swnd.buffer[seq] == -1) {
            return seq;
        }
        seq = (seq + 1) % MAX_SEQ_NUM;
    } while (seq != start);
    
    return -1;
}

int k_socket(int domain, int type, int protocol) {
    initialize_resources();
    setup_semaphores();
    
    if (domain != AF_INET || type != SOCK_KTP) {
        errno = EINVAL;
        return -1;
    }
    
    ACQUIRE_SEM(semid_SM);
    int connection_index = find_connection_slot();

    if (connection_index == -1) {
        RELEASE_SEM(semid_SM);
        errno = ENOSPC;
        return -1;
    }
    
    RELEASE_SEM(semid_SM);
    
    ACQUIRE_SEM(semid_net_socket);
    
    if (connection_index == -1) {
        clean_net_socket(net_socket);
        RELEASE_SEM(semid_net_socket);
        errno = ENOSPC;
        return -1;
    }
    
    RELEASE_SEM(semid_net_socket);
    
    RELEASE_SEM(semid_init);
    ACQUIRE_SEM(semid_ktp);
    
    RELEASE_SEM(semid_net_socket);
    
    if (net_socket->socket_id < 0) {
        errno = net_socket->err_code;
        clean_net_socket(net_socket);
        RELEASE_SEM(semid_net_socket);
        return -1;
    }
    
    int socket_descriptor = net_socket->socket_id;
    RELEASE_SEM(semid_net_socket);
    
    ACQUIRE_SEM(semid_SM);
    
    SM[connection_index].free = 0;
    SM[connection_index].pid = getpid();
    SM[connection_index].udp_sockid = socket_descriptor;
    
    ACQUIRE_SEM(semid_net_socket);
    clean_net_socket(net_socket);
    RELEASE_SEM(semid_net_socket);
    
    init_connection_slot(connection_index);
    
    RELEASE_SEM(semid_SM);
    
    return connection_index;
}

int k_bind(char src_ip[], uint16_t src_port, char dest_ip[], uint16_t dest_port) {
    initialize_resources();
    setup_semaphores();
    
    int socket_index = find_process_socket();
    
    ACQUIRE_SEM(semid_SM);
    
    if (socket_index == -1) {
        RELEASE_SEM(semid_SM);
        errno = EINVAL;
        return -1;
    }
    
    ACQUIRE_SEM(semid_net_socket);
    
    net_socket->socket_id = SM[socket_index].udp_sockid;
    strncpy(net_socket->ip_addr, src_ip, 16);
    net_socket->port = src_port;
    
    RELEASE_SEM(semid_net_socket);
    
    RELEASE_SEM(semid_init);
    ACQUIRE_SEM(semid_ktp);
    
    RELEASE_SEM(semid_net_socket);
    
    if (net_socket->socket_id < 0) {
        errno = net_socket->err_code;
        clean_net_socket(net_socket);
        RELEASE_SEM(semid_net_socket);
        RELEASE_SEM(semid_SM);
        return -1;
    }
    
    clean_net_socket(net_socket);
    RELEASE_SEM(semid_net_socket);
    
    strncpy(SM[socket_index].ip_addr, dest_ip, 16);
    SM[socket_index].port = dest_port;
    
    RELEASE_SEM(semid_SM);
    
    return 0;
}

ssize_t k_sendto(int sockfd, const void *buf, size_t len, int flags,
                const struct sockaddr *dest_addr, socklen_t addrlen) {
    initialize_resources();
    setup_semaphores();
    
    if (sockfd < 0 || sockfd >= N) {
        errno = EINVAL;
        return -1;
    }
    
    struct sockaddr_in *remote = (struct sockaddr_in *)dest_addr;
    char target_ip[16];
    
    if (inet_ntop(AF_INET, &remote->sin_addr, target_ip, 16) == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    uint16_t target_port = ntohs(remote->sin_port);
    
    ACQUIRE_SEM(semid_SM);
    
    if (SM[sockfd].free) {
        errno = EINVAL;
        RELEASE_SEM(semid_SM);
        return -1;
    }
    
    if (strcmp(SM[sockfd].ip_addr, target_ip) != 0 || SM[sockfd].port != target_port) {
        errno = ENOTCONN;
        RELEASE_SEM(semid_SM);
        return -1;
    }
    
    if (SM[sockfd].send_buffer_size <= 0) {
        errno = ENOBUFS;
        RELEASE_SEM(semid_SM);
        return -1;
    }
    
    int sequence = find_next_sequence(sockfd);
    int buffer_slot = find_buffer_slot(sockfd);
    
    if (sequence == -1 || buffer_slot == -1) {
        errno = ENOBUFS;
        RELEASE_SEM(semid_SM);
        return -1;
    }
    
    SM[sockfd].swnd.buffer[sequence] = buffer_slot;
    memcpy(SM[sockfd].send_buffer[buffer_slot], buf, len);
    SM[sockfd].sendbufferlen[buffer_slot] = len;
    SM[sockfd].send_buffer_size--;
    
    SM[sockfd].last_send_time[sequence] = -1;
    
    RELEASE_SEM(semid_SM);
    
    return len;
}

ssize_t k_recvfrom(int sockfd, void* buf, size_t len, int flags,
                  struct sockaddr *src_addr, socklen_t *addrlen) {
    initialize_resources();
    setup_semaphores();
    
    ACQUIRE_SEM(semid_SM);
    
    if (sockfd < 0 || sockfd >= N || SM[sockfd].free) {
        errno = EINVAL;
        RELEASE_SEM(semid_SM);
        return -1;
    }
    
    int base_idx = SM[sockfd].recv_buffer_index;
    if (!SM[sockfd].recv_buffer_live[base_idx]) {
        errno = ENOMSG;
        RELEASE_SEM(semid_SM);
        return -1;
    }
    
    int data_len = SM[sockfd].rcvbufferlen[base_idx];
    int copy_len = (data_len <= len) ? data_len : len;
    
    memcpy(buf, SM[sockfd].recv_buffer[base_idx], copy_len);
    
    SM[sockfd].recv_buffer_live[base_idx] = 0;
    
    int sequence_num = -1;
    for (int seq = 0; seq < MAX_SEQ_NUM; seq++) {
        if (SM[sockfd].rwnd.buffer[seq] == base_idx) {
            sequence_num = seq;
            break;
        }
    }
    
    if (sequence_num >= 0) {
        SM[sockfd].rwnd.buffer[sequence_num] = -1;
        
        int new_seq = (sequence_num + 10) % MAX_SEQ_NUM;
        SM[sockfd].rwnd.buffer[new_seq] = base_idx;
        
        SM[sockfd].rwnd.length++;
        
        SM[sockfd].recv_buffer_index = (base_idx + 1) % 10;
    }
    
    RELEASE_SEM(semid_SM);
    
    if (src_addr != NULL && addrlen != NULL) {
        struct sockaddr_in* addr = (struct sockaddr_in*)src_addr;
        addr->sin_family = AF_INET;
        addr->sin_port = htons(SM[sockfd].port);
        inet_pton(AF_INET, SM[sockfd].ip_addr, &addr->sin_addr);
        *addrlen = sizeof(struct sockaddr_in);
    }
    
    return copy_len;
}

int k_close(int sockfd) {
    initialize_resources();
    setup_semaphores();
    
    if (sockfd < 0 || sockfd >= N) {
        errno = EINVAL;
        return -1;
    }
    
    ACQUIRE_SEM(semid_SM);
    
    if (SM[sockfd].free) {
        errno = EINVAL;
        RELEASE_SEM(semid_SM);
        return -1;
    }
    
    if (SM[sockfd].pid != getpid()) {
        errno = EPERM;
        RELEASE_SEM(semid_SM);
        return -1;
    }
    
    SM[sockfd].free = 1;
    
    RELEASE_SEM(semid_SM);
    
    return 0;
}