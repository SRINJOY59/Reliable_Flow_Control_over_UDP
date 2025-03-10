#ifndef KSOCKET_H
#define KSOCKET_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>



#define T 5
#define p 0.50
#define SOCK_KTP 5
#define N 10
#define WND_SIZE 10
#define ACQUIRE_SEM(s) semop(s, &pop, 1)
#define RELEASE_SEM(s) semop(s, &vop, 1)
#define MAX_SEQ_NUM 256




typedef struct window{
  int buffer[256];  
  int length;
  int start;
}window;

typedef struct Shared_Mem{
  int free;
  char recv_buffer[WND_SIZE][MAX_SEQ_NUM*2];
  int recv_buffer_live[WND_SIZE];
  int rcvbufferlen[WND_SIZE];
  int recv_buffer_index;
  char send_buffer[WND_SIZE][MAX_SEQ_NUM*2];
  int send_buffer_size;
  int sendbufferlen[WND_SIZE];
  time_t last_send_time[MAX_SEQ_NUM];
  window swnd;
  window rwnd;
  pid_t pid;
  int udp_sockid;
  char ip_addr[16];
  uint16_t port;
  int non_space;
}SHARED_MEM;


typedef struct network_socket NETWORK_SOCKET;
int k_socket(int domain,int type,int protocol);
int k_bind(char src_ip[],uint16_t src_port,char dest_ip[],uint16_t dest_port);
ssize_t k_sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t k_recvfrom(int sockfd, void* buf, size_t len, int flags, struct sockaddr * src_addr, socklen_t *addrlen);
int k_close(int sockfd);
int drop_Message();


extern SHARED_MEM *SM;
extern NETWORK_SOCKET* net_socket;
extern struct sembuf pop,vop;
extern int semid_SM,semid_net_socket;
extern int shmid_SM,shmid_net_socket;
extern int semid_init,semid_ktp;


#endif
