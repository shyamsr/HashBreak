#include "lsp.h"
#include "lspmessage.pb-c.h"
#include "utils.h"

double epoch_lth = _EPOCH_LTH;
int epoch_cnt = _EPOCH_CNT;
double drop_rate = _DROP_RATE;

/*
 *
 *
 *				LSP RELATED FUNCTIONS
 *
 *
 */  

void lsp_set_epoch_lth(double lth){epoch_lth = lth;}
void lsp_set_epoch_cnt(int cnt){epoch_cnt = cnt;}
void lsp_set_drop_rate(double rate){drop_rate = rate;}


/*
 *
 *
 *				CLIENT RELATED FUNCTIONS
 *
 *
 */  


lsp_client* lsp_client_create(const char* src, int port)
{
	int     csd;                                /* all socket descriptors */
        struct sockaddr_in      sock;               /* this is the socket struct  for client */
        lsp_client*     cp;

	char    server_ip[35];
        struct  hostent* hp;

	void* temp_buf;
	int server_addr_len;
	struct sockaddr_in  server_sd;
	int msg_len;
	LSPMessage *msg;

	void*   (*init_read_thread)() = &read_from_server;
	void*   (*init_write_thread)() = &write_to_server;
	void*   (*init_epoch_thread)() = &epoch_clientside;
        pthread_attr_t  attr_read, attr_write, attr_epoch;
        pthread_t       tid_read, tid_write, tid_epoch;


        memset(&sock, 0, sizeof(sock));


        csd = socket(AF_INET, SOCK_DGRAM, 0);  /* got a socket descriptor for server*/
        if(csd == -1) {
                perror("Error creating socket");
                return NULL;
        }

        sock.sin_family = AF_INET;
        sock.sin_port   = htons(port);
	strcpy(server_ip, src);
        if ( (hp = gethostbyname(server_ip)) == NULL) {        /*converting to proper IP*/
                perror("Failed in gethostbyname() ");
                return NULL;
        }
        memcpy((char*)&sock.sin_addr.s_addr, hp->h_addr, hp->h_length);

	cp = (lsp_client*)malloc(sizeof(lsp_client));
	memset(cp, 0, sizeof(lsp_client));
	cp->sd = csd;
	cp->conn_id = 0;
	cp->seq_num = 0;
	cp->expected_seq_num = 1;
	memcpy(&cp->server_sd, &sock, sizeof(sock));

        LSPMessage init_msg = LSPMESSAGE__INIT;
	int init_len;
	void* init_buf;
	struct timeval select_tv;
	int sel_ret;
	fd_set  allfd, modfd;
	
	FD_ZERO(&allfd);        /* first, clear the allfd set */
        FD_SET(cp->sd, &allfd);    /* adding chatroom to the set */
	
	init_msg.connid = 0;
        init_msg.seqnum = 0;
        init_msg.payload.data = NULL;
        init_msg.payload.len = 0;

        init_len  = lspmessage__get_packed_size(&init_msg);
        init_buf = malloc(init_len);
        lspmessage__pack(&init_msg, init_buf);

        sendto(cp->sd, init_buf, init_len, 0, (struct sockaddr*)&cp->server_sd, sizeof(cp->server_sd));


	temp_buf = malloc(sizeof(BUF_LEN));
        server_addr_len = sizeof(server_sd);

	// srs: insert select timeout here !!!
	while(1) {
		modfd = allfd;
		select_tv.tv_sec = epoch_lth;
        	select_tv.tv_usec = 0;
		sel_ret = select(cp->sd + 1, &modfd, NULL, NULL, &select_tv);
		if (sel_ret == 0) {
			sendto(cp->sd, init_buf, init_len, 0, (struct sockaddr*)&cp->server_sd, sizeof(cp->server_sd));
		}
		if (sel_ret == 1) {
			msg_len = recvfrom(cp->sd, temp_buf, BUF_LEN, 0, (struct sockaddr*)&server_sd,(socklen_t *)&server_addr_len);
			if(msg_len == -1 || msg_len == 0) {
         		       continue;
        		}
			break;
		}
	}

	msg = lspmessage__unpack(NULL, msg_len, temp_buf);
	if(msg->connid > 0 && msg->seqnum == 0 && msg->payload.len == 0) {// its an ACK for connection request
		cp->conn_id = msg->connid;

                cp->allow_write = 1;
                cp->expected_seq_num = 1;
                cp->seq_num = 1;
	} else {
	//srs: its an invalid response from server: resend connection request
	}

        lspmessage__free_unpacked(msg,NULL);
	free(temp_buf);
	free(init_buf);
	//printf("Created client connid: %d\n",cp->conn_id);
	
	cp->rq_head = NULL;
	cp->wq_head = NULL;
	pthread_attr_init(&attr_read);
        pthread_attr_setdetachstate(&attr_read, PTHREAD_CREATE_DETACHED);
	pthread_mutex_init(&cp->read_q_mutex, NULL);
        pthread_create(&tid_read, &attr_read, init_read_thread, cp);

	pthread_attr_init(&attr_write);
        pthread_attr_setdetachstate(&attr_write, PTHREAD_CREATE_DETACHED);
	pthread_mutex_init(&cp->write_q_mutex, NULL);
        pthread_create(&tid_write, &attr_write, init_write_thread, cp);

	pthread_attr_init(&attr_epoch);
        pthread_attr_setdetachstate(&attr_epoch, PTHREAD_CREATE_DETACHED);
        pthread_mutex_init(&cp->timer_mutex, NULL);
        pthread_create(&tid_epoch, &attr_epoch, init_epoch_thread, cp);

	return cp;
	
}

int lsp_client_read(lsp_client* a_client, uint8_t* pld)
{
	uint8_t* buf;
        int len;
	uint32_t conn_id;
	//srs: take care of offline/disconnected server, if offline return NULL here itself

	if(a_client->server_disconnected ==1 && a_client->conn_id > 0) { // this server is offline
                        return -1;
        }

        buf = (uint8_t*) malloc(BUF_LEN);

        //lock with read_q_mutex
        //srs: return conn_id also
        pthread_mutex_lock(&a_client->read_q_mutex);

        if((len=dequeue(&a_client->rq_head, buf, &conn_id)) != 0) {
                pthread_mutex_unlock(&a_client->read_q_mutex);
                memcpy(pld, buf, len);
                free(buf);
                return len;
        }
        pthread_mutex_unlock(&a_client->read_q_mutex);
        free(buf);

        return len;

	
}

bool lsp_client_write(lsp_client* a_client, uint8_t* pld, int lth)
{
        void* buf = malloc(lth);
        memcpy(buf, pld, lth);
        pthread_mutex_lock(&a_client->write_q_mutex);
        enqueue(&a_client->wq_head, buf, lth, a_client->conn_id, &a_client->write_q_mutex);
        pthread_mutex_unlock(&a_client->write_q_mutex);
        return 1;
	
}

bool lsp_client_close(lsp_client* a_client)
{
	close(a_client->sd);
	if (a_client->resend_length != 0)
	    free(a_client->resend_buffer);
	if (a_client->resend_ack_length != 0)
	    free(a_client->resend_ack_buffer);

	return 0;
	
}

/*
 *
 *
 *				SERVER RELATED FUNCTIONS
 *
 *
 */  

lsp_server* server_head = NULL;

lsp_server* lsp_server_create(int port)
{
	int     ssd;   	                            /* all socket descriptors */
        struct sockaddr_in      sock;               /* this is the socket struct  for server */
	lsp_server*	temp;
	lsp_server*	sp;

	void*   (*init_read_thread)() = &read_from_clients;
	void*   (*init_write_thread)() = &write_to_clients;
	void*   (*init_epoch_thread)() = &epoch_for_a_client;
        pthread_attr_t  attr_read, attr_write, attr_epoch;
        pthread_t       tid_read, tid_write, tid_epoch;


	temp = server_head;

        memset(&sock, 0, sizeof(sock));


        ssd = socket(AF_INET, SOCK_DGRAM, 0);  /* got a socket descriptor for server*/
        if(ssd == -1) {
                perror("Error creating socket");
                return NULL;
        }

        sock.sin_family = AF_INET;
        sock.sin_port   = htons(port);
        sock.sin_addr.s_addr = INADDR_ANY; // does not take care of cases where server has multiple interfaces

        if(bind(ssd, (struct sockaddr*)&sock, sizeof(struct sockaddr_in)) == -1) {
                perror("Error in bind()");
                return NULL;
        }
	
	if(temp == NULL ) { // head is NULL
		temp = (lsp_server*)malloc(sizeof(lsp_server));
		memset(temp, 0, sizeof(lsp_server));
		temp->sd = ssd;
                temp->next = NULL;
                sp = temp;
	} else {
		while(temp->next != NULL) 
			temp = temp->next;

		temp->next = (lsp_server*)malloc(sizeof(lsp_server));
		memset(temp->next, 0, sizeof(lsp_server));
		temp->next->sd = ssd;
		temp->next->next = NULL;
		sp = temp->next;
	}

	sp->rq_head = NULL;
	sp->wq_head = NULL;
	pthread_mutex_init(&sp->master_lock, NULL);
	pthread_attr_init(&attr_read);
        pthread_attr_setdetachstate(&attr_read, PTHREAD_CREATE_DETACHED);
	pthread_mutex_init(&sp->read_q_mutex, NULL);
        pthread_create(&tid_read, &attr_read, init_read_thread, sp);

	pthread_attr_init(&attr_write);
        pthread_attr_setdetachstate(&attr_write, PTHREAD_CREATE_DETACHED);
	pthread_mutex_init(&sp->write_q_mutex, NULL);
        pthread_create(&tid_write, &attr_write, init_write_thread, sp);

	pthread_attr_init(&attr_epoch);
	pthread_attr_setdetachstate(&attr_epoch, PTHREAD_CREATE_DETACHED);
	pthread_create(&tid_epoch, &attr_epoch, init_epoch_thread, sp);

	

	return sp;

}

int lsp_server_read(lsp_server* a_srv, void* pld, uint32_t* conn_id) {

	//srs: take care of offline clients, if offline return NULL here itself
	uint8_t* buf;
	int len;
	int i;
	uint32_t temp_conn_id;

	for(i=1;i<MAX_CLIENTS;i++) { // check for disconnected clients
		if(a_srv->clients[i].client_disconnected ==1 && a_srv->clients[i].conn_id > 0) { // this client is offline, cleanup
			temp_conn_id = a_srv->clients[i].conn_id;
			pthread_mutex_lock(&a_srv->master_lock);
			if(a_srv->clients[i].resend_ack_length != 0)
				free(a_srv->clients[i].resend_ack_buffer);	
			if(a_srv->clients[i].resend_length != 0)
				free(a_srv->clients[i].resend_buffer);
			//printf("reseting client id %d\n",a_srv->clients[i].conn_id );
			a_srv->clients[i].conn_id = 0;
			a_srv->clients[i].client_disconnected = 0;
			memset(&a_srv->clients[i], 0, sizeof(struct client_summary));
			pthread_mutex_unlock(&a_srv->master_lock);
			*conn_id = temp_conn_id;
			return 0;
		}
	}

        buf = (uint8_t*) malloc(BUF_LEN);
	
	//lock with read_q_mutex
	//srs: return conn_id also HANDLE this !
	pthread_mutex_lock(&a_srv->read_q_mutex);

	if((len=dequeue(&a_srv->rq_head, buf, conn_id)) != 0) {
		pthread_mutex_unlock(&a_srv->read_q_mutex);
		memcpy(pld, buf, len);
		free(buf);
		return len;
	}
	pthread_mutex_unlock(&a_srv->read_q_mutex);
	free(buf);
	*conn_id = 0;
	return len;
}


bool lsp_server_write(lsp_server* a_srv, void* pld, int lth, uint32_t conn_id) {
	void* buf = malloc(lth);
	memcpy(buf, pld, lth);
	pthread_mutex_lock(&a_srv->write_q_mutex);
	enqueue(&a_srv->wq_head, buf, lth, conn_id, &a_srv->write_q_mutex);
	pthread_mutex_unlock(&a_srv->write_q_mutex);
	return 1;
}



bool lsp_server_close(lsp_server* a_srv, uint32_t conn_id)
{
	int i;
	close(a_srv->sd);
	for(i=1;i<MAX_CLIENTS;i++){
	    if(a_srv->clients[i].resend_length != 0)
		free(a_srv->clients[i].resend_buffer);
	    if(a_srv->clients[i].resend_ack_length != 0)
		free(a_srv->clients[i].resend_ack_buffer);
	}
	return 0;
	
}
