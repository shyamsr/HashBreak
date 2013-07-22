#pragma once


#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <errno.h>
#include <strings.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>

// Global Parameters. For both server and clients.

#define _EPOCH_LTH 2.0
#define _EPOCH_CNT 5
#define _DROP_RATE 0.0
#define MAX_CLIENTS 135
#define BUF_LEN 2048

void lsp_set_epoch_lth(double lth);
void lsp_set_epoch_cnt(int cnt);
void lsp_set_drop_rate(double rate);

struct queue_element {
        void*   data;
        int     data_len;
        uint32_t conn_id;
        struct queue_element* next;
};

typedef struct
{
	int sd;
	struct sockaddr_in server_sd;
	uint32_t conn_id;
	uint32_t seq_num;		// for what the client writes
	uint32_t expected_seq_num;	// for what the client reads

	int		allow_write;		//1 means ack was received for previous write, and can send a new write
	int 		server_disconnected;	//1 = server is offline
	unsigned int    timer;
        unsigned int    epoch_count;
	int		resend_ack_length;
	void*		resend_ack_buffer;
	int 		resend_length;
	void*		resend_buffer;
	
	pthread_mutex_t timer_mutex;
	pthread_mutex_t read_q_mutex;
	pthread_mutex_t write_q_mutex;
	struct queue_element* rq_head;
	struct queue_element* wq_head;
	
} lsp_client;

lsp_client* lsp_client_create(const char* dest, int port);
int lsp_client_read(lsp_client* a_client, uint8_t* pld);
bool lsp_client_write(lsp_client* a_client, uint8_t* pld, int lth);
bool lsp_client_close(lsp_client* a_client);

/* Server specific Data Structures */



struct client_summary
{
	int 		sd;
	uint32_t 	conn_id;
	uint32_t	seq_num; // for the data that is written by server
	uint32_t	expected_seq_num; // for data that is read by server
	struct sockaddr_in client_sd;

	pthread_mutex_t timer_mutex;

	int		allow_write;		//1 means ack was received for previous write, and can send a new write
	int 		client_disconnected;	//1 = client is offline, read will take care of cleanup
	unsigned int 	timer;
	unsigned int 	epoch_count;
	int		resend_ack_length;
	void*		resend_ack_buffer;
	int 		resend_length;
	void*		resend_buffer;

};

struct serverLSP
{
	int sd;
	struct sockaddr_in 	server_sd;
	struct client_summary  	clients[MAX_CLIENTS];	

	pthread_mutex_t master_lock;
	pthread_mutex_t read_q_mutex;
	pthread_mutex_t write_q_mutex;
	struct queue_element* rq_head;
	struct queue_element* wq_head;
 
	struct serverLSP* next;
};
typedef struct serverLSP lsp_server;



/* Server functions */

lsp_server* lsp_server_create(int port);
int  lsp_server_read(lsp_server* a_srv, void* pld, uint32_t* conn_id);
bool lsp_server_write(lsp_server* a_srv, void* pld, int lth, uint32_t conn_id);
bool lsp_server_close(lsp_server* a_srv, uint32_t conn_id);

int dequeue(struct queue_element** , void* ,uint32_t* );
int enqueue(struct queue_element** ,void* ,int ,int ,pthread_mutex_t* );
int stringTohex(const char *);
void stringTonum(char* ,int ,int64_t* );
void numTostring(char *,int64_t ,int );
