#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#include <pthread.h>

#include "mygbn.h"

void *t_timeout(void *argv) {
    struct mygbn_sender * mygbn_sender = (struct mygbn_sender *)argv;
    struct timespec ts;
    struct timeval tp;
    while(1) {
        pthread_mutex_lock(&(mygbn_sender->lock_timeout));
        gettimeofday(&tp, NULL);
        ts.tv_sec = tp.tv_sec;
        ts.tv_nsec = tp.tv_usec * 1000;
        ts.tv_sec += mygbn_sender->timeout;
        int rc;
        //printf("timer started\n");
        rc = pthread_cond_timedwait(&(mygbn_sender->time_signal), &(mygbn_sender->lock_timeout), &ts);
        if (rc == ETIMEDOUT) {
            pthread_cond_signal(&(mygbn_sender->block_signal));
        } 
        else {
        
        }
        pthread_mutex_unlock(&(mygbn_sender->lock_timeout));
    }
    return NULL;
}

void *t_ack_packets(void *argv) {
    struct mygbn_sender *mygbn_sender = (struct mygbn_sender*)argv;
    while(1) {
        int size;
        struct MYGBN_Packet ack;

        size = recvfrom(mygbn_sender->sd, &ack, sizeof(ack), 0, NULL, NULL); // receive ACK from server

        if(ack.type == 0xA1) {
            printf("\n");
            printf("GBN::Receiving ACK[%d] packet from server......\n\n", ntohl(ack.seqNum));
            pthread_mutex_lock(&(mygbn_sender->lock));
            if(ntohl(ack.seqNum) >= mygbn_sender->base) {
                mygbn_sender->base = ntohl(ack.seqNum) + 1;
                pthread_mutex_unlock(&mygbn_sender->lock);
                pthread_cond_signal(&(mygbn_sender->block_signal));
            }

            else {
                pthread_mutex_unlock(&(mygbn_sender->lock));
            }
        }

        if(ntohl(ack.seqNum) == mygbn_sender->total + 1) {
            break;
        }
    }
    return NULL;
}

void *t_end_packets(void *argv) {
    struct mygbn_sender * mygbn_sender = (struct mygbn_sender *)argv;
    while(1) {
        struct MYGBN_Packet ack;
        int size = recvfrom(mygbn_sender->sd, &ack, sizeof(ack), 0, NULL, NULL);
        if(ack.type == 0xA1){
            printf("\n");
            printf("GBN::Receiving ACK[%d] packet from server......\n\n", ntohl(ack.seqNum));

            pthread_mutex_lock(&(mygbn_sender->lock));
            mygbn_sender->base = ntohl(ack.seqNum) + 1;
            pthread_mutex_unlock(&(mygbn_sender->lock));
            pthread_cond_signal(&(mygbn_sender->block_signal));
            break;
        }
    }
  return NULL;
}


struct MYGBN_Packet ack_packet_init(int seq) {

  struct MYGBN_Packet ack_packet;

  strncpy(ack_packet.protocol, "gbn", 3);
  memset(&ack_packet, 0, sizeof(ack_packet));
  ack_packet.type = 0xA1;
  ack_packet.seqNum = htonl(seq);  
  ack_packet.length = htonl(sizeof(struct MYGBN_Packet));
  return ack_packet;
}

void mygbn_init_sender(struct mygbn_sender* mygbn_sender, char* ip, int port, int N, int timeout){

  printf("\n");
  printf("UDP::Initialize address for sender......\n");

  mygbn_sender->sd = socket(AF_INET, SOCK_DGRAM, 0);
  
  struct sockaddr_in server_address;
  memset(&server_address, 0, sizeof(struct sockaddr_in));
  inet_pton(AF_INET, ip, &(server_address.sin_addr));
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(port);

  mygbn_sender->server_addr = server_address;

  printf("UDP::Success to initialize address for sender!!!\n\n");

  mygbn_sender->window_size = N;
  mygbn_sender->base = 1;
  mygbn_sender->timeout = timeout;
  mygbn_sender->no_of_packet = 0;

  pthread_mutex_init(&mygbn_sender->lock, PTHREAD_MUTEX_TIMED_NP);
  pthread_mutex_init(&mygbn_sender->lock_timeout, PTHREAD_MUTEX_TIMED_NP);
  pthread_cond_init(&mygbn_sender->time_signal, NULL);
  pthread_cond_init(&mygbn_sender->block_signal, NULL);
}

int mygbn_send(struct mygbn_sender* mygbn_sender, unsigned char* buf, int len) {

    printf("\n");
    printf("###################################\n\n");

    int index;
    int position;
    int no_of_packet;
    struct MYGBN_Packet *packet;
    pthread_t __t_ack_packets, __t_timeout;
    mygbn_sender->base = 1;
    mygbn_sender->no_of_packet = 0;

    no_of_packet = (len % MAX_PAYLOAD_SIZE == 0) ? (len / MAX_PAYLOAD_SIZE + 1) : (len / MAX_PAYLOAD_SIZE + 2);
    packet = (struct MYGBN_Packet*)malloc(sizeof(struct MYGBN_Packet) * no_of_packet);

    mygbn_sender->total = no_of_packet - 1; // 6 + 5 - 1 = 10
    position = mygbn_sender->base;
    index = 0;

    for(int i = 0; i < no_of_packet; i++) {
        strncpy(packet[i].protocol, "gbn", 3);
        if(i == no_of_packet - 1) {
            packet[i].type = 0xA2;
            packet[i].seqNum = htonl(i + position);
            packet[i].length = htonl(sizeof(struct MYGBN_Packet));
        }
        else if (i == no_of_packet - 2) {
            packet[i].type = 0xA0;
            packet[i].seqNum = htonl(i + position);
            packet[i].length = htonl(sizeof(struct MYGBN_Packet) + len - MAX_PAYLOAD_SIZE*i);
            for(int j = 0; j < len - MAX_PAYLOAD_SIZE * i; j++){
                packet[i].payload[j] = buf[index++];
            }
        }
        else {
            packet[i].type = 0xA0;
            packet[i].seqNum = htonl(i + position);
            packet[i].length = htonl(sizeof(struct MYGBN_Packet) + MAX_PAYLOAD_SIZE);
            for(int j = 0; j < MAX_PAYLOAD_SIZE; j++){
                packet[i].payload[j] = buf[index++];
            }
        }
    }

    pthread_create(&__t_ack_packets, NULL, t_ack_packets, (void*)mygbn_sender);

    pthread_mutex_lock(&(mygbn_sender->lock));
    for(int i = 0; i < mygbn_sender->window_size && i < no_of_packet; i++) {
        sendto(mygbn_sender->sd, &packet[i], ntohl(packet[i].length), 0, (struct sockaddr*)&(mygbn_sender->server_addr), sizeof(mygbn_sender->server_addr));
        (i == no_of_packet -1) ? printf("UDP::Success to send end packet[%d] to the server!!!\n", i + 1) : printf("UDP::Success to send packet[%d/%d] to the server!!!\n", i + 1, no_of_packet);
    }

    pthread_create(&__t_timeout, NULL, t_timeout, (void*)mygbn_sender);


    while(mygbn_sender->base <= mygbn_sender->total + 1) {
        int beginning = mygbn_sender->base;

        pthread_cond_wait(&(mygbn_sender->block_signal), &(mygbn_sender->lock));

        if(mygbn_sender->base > beginning) {
            int start = beginning + mygbn_sender->window_size;

            for(int i = start; (i < mygbn_sender->base + mygbn_sender->window_size) && (i < mygbn_sender->total + 2); i++) {
                sendto(mygbn_sender->sd, &packet[i-position], ntohl(packet[i-position].length), 0, (struct sockaddr*)&(mygbn_sender->server_addr), sizeof(mygbn_sender->server_addr));
                (i == no_of_packet -1) ? printf("A: UDP::Success to send end packet[%d] to the server!!!\n", i) : printf("A: UDP::Success to send packet[%d/%d] to the server!!!\n", i, no_of_packet - 1);
            }

            pthread_mutex_lock(&(mygbn_sender->lock_timeout));
            pthread_cond_signal(&(mygbn_sender->time_signal));
            pthread_mutex_unlock(&(mygbn_sender->lock_timeout));
        }
        else {
            printf("time\n");
            
            for(int i = beginning;i < beginning + mygbn_sender->window_size && (i < mygbn_sender->total + 3); i++){
                sendto(mygbn_sender->sd,&packet[i-position-1],ntohl(packet[i-position-1].length),0, (struct sockaddr*)&(mygbn_sender->server_addr), sizeof(mygbn_sender->server_addr));
                (i == no_of_packet -1) ? printf("A: UDP::Success to send end packet[%d] to the server!!!\n", i) : printf("A: UDP::Success to send packet[%d/%d] to the server!!!\n", i, no_of_packet - 1);
            }

            pthread_mutex_lock(&(mygbn_sender->lock_timeout));
            pthread_cond_signal(&(mygbn_sender->time_signal));
            pthread_mutex_unlock(&(mygbn_sender->lock_timeout));
        }

        pthread_mutex_unlock(&(mygbn_sender->lock));
    }

    return len;
}

void mygbn_close_sender(struct mygbn_sender* mygbn_sender){
    int times;
    int count;
    int no_of_packet;
    int beginning;

    pthread_t __end_thread, __timeout_thread;

    //init
    struct MYGBN_Packet endpacket;
    strncpy(endpacket.protocol, "gbn", 3);
    memset(&endpacket, 0, sizeof(endpacket));
    endpacket.type = 0xA2;
    endpacket.seqNum = htonl(0);  
    endpacket.length = htonl(sizeof(struct MYGBN_Packet));

    times = 0;
    count = 0;
    beginning  = 0;
    pthread_create(&__end_thread, NULL, t_end_packets, (void*)mygbn_sender);

    pthread_mutex_lock(&(mygbn_sender->lock));
    sendto(mygbn_sender->sd, &endpacket, ntohl(endpacket.length), 0, (struct sockaddr*)&(mygbn_sender->server_addr), sizeof(mygbn_sender->server_addr));

    pthread_create(&__timeout_thread, NULL, t_timeout, (void*)mygbn_sender);

    while(count < 3) {
        pthread_cond_wait(&(mygbn_sender->block_signal), &(mygbn_sender->lock));
        printf("mygbn_sender->base: %d\n", mygbn_sender->base);
        printf("beginning: %d\n", beginning);
        if (mygbn_sender->base > beginning) {
            times++;
            close(mygbn_sender->sd);
            break;
        }
        else {
            printf("count: %d\n", count);
            sendto(mygbn_sender->sd, &endpacket, ntohl(endpacket.length), 0, (struct sockaddr*)&(mygbn_sender->server_addr), sizeof(mygbn_sender->server_addr));
            count++;
            pthread_mutex_lock(&(mygbn_sender->lock_timeout));
            pthread_cond_signal(&(mygbn_sender->time_signal));
            pthread_mutex_unlock(&(mygbn_sender->lock_timeout));
        }
        pthread_mutex_unlock(&(mygbn_sender->lock));
    }
    
    if(times == 0) {
        printf("ERR: Server does not return endpacket\n");
        close(mygbn_sender->sd);
    }
}

void mygbn_init_receiver(struct mygbn_receiver* mygbn_receiver, int port){
  
  mygbn_receiver->sd = socket(AF_INET, SOCK_DGRAM, 0);
  mygbn_receiver->seqNum = 1;
  
  struct sockaddr_in server_address;
  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(port);
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);
  
  mygbn_receiver->server_addr = server_address;

  printf("\n");
  printf("UDP::Success to initialize address for receiver!!!\n");

  bind(mygbn_receiver->sd, (struct sockaddr*)&mygbn_receiver->server_addr, sizeof(struct sockaddr));

  printf("UDP::Success to bind to the server!!!\n\n");
}

int mygbn_recv(struct mygbn_receiver* mygbn_receiver, unsigned char* buf, int len){
    int count = 0;

    printf("\n");
    printf("###################################\n\n");


    while(count <= len) {

        if(count == 4096){
            printf("2048 + 2048\n");
            break;
        }

        printf("Count: %d ******\n", count);
        int size;
        struct MYGBN_Packet packet;
        struct sockaddr_in addr;
        socklen_t addr_size;
        
        addr_size = sizeof(struct sockaddr_in);
        size = recvfrom(mygbn_receiver->sd, &packet, sizeof(packet), 0, (struct sockaddr*)&addr, &addr_size);
        
        printf("\n");
        printf("GBN::Success to receive packet with seqNum %d\n", ntohl(packet.seqNum));

        // printf("pack seq %d\n", ntohl(packet.seqNum));
        // printf("receiver seq %d\n\n", mygbn_receiver->seqNum);
        if(packet.type == 0xA2 && ntohl(packet.seqNum) <= mygbn_receiver->seqNum) {
            struct MYGBN_Packet ack;
            ack = ack_packet_init(ntohl(packet.seqNum));
            sendto(mygbn_receiver->sd, &ack, ntohl(ack.length), 0, (struct sockaddr*)&addr, addr_size);
            printf("A: GBN::ACK[%d] packet has been sent to client!!!\n", ntohl(ack.seqNum));
            
            if(ntohl(packet.seqNum) == mygbn_receiver->seqNum) {
                mygbn_receiver->seqNum = 1;

                if(count > 0){
                    return count;
                }
            }
        }

        else if(ntohl(packet.seqNum) == mygbn_receiver->seqNum) {
            int datasize = ntohl(packet.length) - sizeof(struct MYGBN_Packet);
            for(int i = count; i < count + datasize; i++) {
                buf[i] = packet.payload[i - count];
            }

            struct MYGBN_Packet ack;
            memset(&ack, 0, sizeof(ack));
            strncpy(ack.protocol, "gbn", 3);
            ack.type = 0xA1;
            ack.seqNum = htonl(mygbn_receiver->seqNum);  
            ack.length = htonl(sizeof(struct MYGBN_Packet));
            sendto(mygbn_receiver->sd, &ack, ntohl(ack.length), 0, (struct sockaddr*)&addr, addr_size);
            printf("B: GBN::ACK[%d] packet has been sent to client!!!\n", ntohl(ack.seqNum));

            mygbn_receiver->seqNum++;
            count += datasize;
        }

        else {
            struct MYGBN_Packet ack;
            ack = ack_packet_init(mygbn_receiver->seqNum);
            sendto(mygbn_receiver->sd, &ack, ntohl(ack.length), 0, (struct sockaddr*)&addr, addr_size);
            printf("C: GBN::ACK[%d] packet has been sent to client!!!\n", ntohl(ack.seqNum));

        }
    }


    return len;
}

void mygbn_close_receiver(struct mygbn_receiver* mygbn_receiver) {
    close(mygbn_receiver->sd);
}

