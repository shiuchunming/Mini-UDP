#ifndef __MYGBN_H__
#define __MYGBN_H__

#include <pthread.h>

#define MAX_PAYLOAD_SIZE 512

struct MYGBN_Packet {
  unsigned char protocol[3];                  /* protocol string (3 bytes) "gbn" */
  unsigned char type;                         /* type (1 byte) */
  unsigned int seqNum;                        /* sequence number (4 bytes) */
  unsigned int length;                        /* length(header+payload) (4 bytes) */
  unsigned char payload[MAX_PAYLOAD_SIZE];    /* payload data */
} __attribute__((packed));

struct mygbn_sender {
  int sd;
  int window_size;
  int base;
  int timeout;
  int total;
  int no_of_packet;
  struct sockaddr_in server_addr;
  pthread_mutex_t lock;
  pthread_mutex_t lock_timeout;
  pthread_cond_t time_signal;
  pthread_cond_t block_signal;
};

void mygbn_init_sender(struct mygbn_sender* mygbn_sender, char* ip, int port, int N, int timeout);
int mygbn_send(struct mygbn_sender* mygbn_sender, unsigned char* buf, int len);
void mygbn_close_sender(struct mygbn_sender* mygbn_sender);

struct mygbn_receiver {
  int sd;
  int seqNum;
  struct sockaddr_in server_addr;
};

void mygbn_init_receiver(struct mygbn_receiver* mygbn_receiver, int port);
int mygbn_recv(struct mygbn_receiver* mygbn_receiver, unsigned char* buf, int len);
void mygbn_close_receiver(struct mygbn_receiver* mygbn_receiver);

#endif
