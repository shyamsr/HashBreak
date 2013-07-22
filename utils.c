/*Utility functions for lsp.c*/

#include "lsp.h"
#include "lspmessage.pb-c.h"
#include "utils.h"
int enqueue(struct queue_element** q_head, void* data, int len, int conn_id, pthread_mutex_t* lock) {

	struct queue_element* temp;
	
	if(*q_head == NULL ) { // head is NULL
                temp = (struct queue_element*)malloc(sizeof(struct queue_element));
		if(temp==NULL)
			return 0;
                temp->data = data;
		temp->data_len = len;
		temp->conn_id = conn_id;
                temp->next = NULL;
		*q_head = temp;
        } else {
		temp = *q_head;
		assert(temp!=NULL);
                while(temp->next != NULL)
                        temp = temp->next;

                temp->next = (struct queue_element*)malloc(sizeof(struct queue_element));
		if(temp->next==NULL) {
			return 0;
		}
                temp->next->data = data;
                temp->next->data_len = len;
		temp->next->conn_id = conn_id;
                temp->next->next = NULL;
        }
	//srs: handle failure case
	return 0;

}


int dequeue(struct queue_element** q_head, void* data, uint32_t* conn_id ) {
	struct queue_element* temp;
	int len;
	if(*q_head != NULL) {
		temp = (*q_head)->next;
		memcpy(data, (*q_head)->data, (*q_head)->data_len);
		len = (*q_head)->data_len;
		*conn_id = (*q_head)->conn_id;
		free((*q_head)->data);
		free((*q_head));
		*q_head = temp;
		return len;
	}
	*conn_id = 0;
	return 0;
}

void* read_from_clients(void* thread_args)
{
        LSPMessage *msg;
        int msg_len;
        uint8_t* buf;
        struct sockaddr_in  client_sd;
        int client_addr_len;
        uint32_t        connections = 0;
        lsp_server* a_srv = (lsp_server*) thread_args;
        
	while(1) {
        buf = (uint8_t*) malloc(BUF_LEN);
	memset(&client_sd, 0, sizeof(client_sd));
        client_addr_len = sizeof(client_sd);
        msg_len = recvfrom(a_srv->sd, buf, BUF_LEN, 0, (struct sockaddr*)&client_sd,(socklen_t *)&client_addr_len);
	if(msg_len == -1 || msg_len == 0) {
		free(buf);
		continue;
	}
        msg = lspmessage__unpack(NULL, msg_len, buf);
        if(msg == NULL) {
                continue;
        }
	//drop packets according to the drop rate
	if(gen_random() < drop_rate){
	   free(buf);
       	   lspmessage__free_unpacked(msg,NULL);
	   continue;
  	}
/*      What do we want to do with 
        client_sd, seqnum, msg->connid, msg->payload.data, msg->payload.len
        if new conn request connid = 0 and seqnum = 0 and datalen = 0, then initialize the client, send ACK, somehow check for duplicate REQUESTS!
        if its data, valid connid and seqnum and valid data len, then enqueue it
        if its an ACK, valid connid and seqnum but datalen = 0, then set write allow state in global for this connid, free resources
*/

	/* calculate the proper index for clients[] here */
        if(msg->connid == 0 && msg->seqnum == 0 && msg->payload.len == 0) {  //new connection request
		//srs: check for free slot and then return that
		//srs: check for duplicate Requests !!  when you get duplicate requests be sure to update timers
		
		int i = 1; // connection id always starts from 1
		int found = 0;
                while(i<MAX_CLIENTS) {
			if(memcmp(&a_srv->clients[i].client_sd, &client_sd, sizeof(struct sockaddr_in)) == 0) {
				pthread_mutex_lock(&a_srv->clients[msg->connid].timer_mutex);
		                a_srv->clients[msg->connid].epoch_count = 0;
                		pthread_mutex_unlock(&a_srv->clients[msg->connid].timer_mutex);

				found = 1;	
				break;
			
			}
			i++;
		}
		if(found == 1) {
			free(buf);
		        lspmessage__free_unpacked(msg,NULL);
			continue;
		}

		i = 1; // connection id always starts from 1
		pthread_mutex_lock(&a_srv->master_lock);
		while(i<MAX_CLIENTS) {
			if(a_srv->clients[i].conn_id == 0 )
				break;
			i++;
		}
		pthread_mutex_unlock(&a_srv->master_lock);
                connections = i;
		a_srv->clients[connections].allow_write = 1;
                a_srv->clients[connections].sd = a_srv->sd;
                a_srv->clients[connections].conn_id = connections;
                a_srv->clients[connections].expected_seq_num = 1;
                a_srv->clients[connections].seq_num = 1;
                memcpy(&a_srv->clients[connections].client_sd, &client_sd, sizeof(struct sockaddr_in));

                pthread_mutex_init(&a_srv->clients[connections].timer_mutex, NULL);

                /* send ACK */
                void* ack_buf;
                int len;
                LSPMessage ack_msg = LSPMESSAGE__INIT;

                ack_msg.connid = connections;
                ack_msg.seqnum = 0;
                ack_msg.payload.data = NULL;
                ack_msg.payload.len = 0;
                len = lspmessage__get_packed_size(&ack_msg);
                ack_buf = malloc(len);
                lspmessage__pack(&ack_msg, ack_buf);


                pthread_mutex_lock(&a_srv->clients[connections].timer_mutex);

		a_srv->clients[connections].epoch_count = 0;
                a_srv->clients[connections].resend_ack_length = len;
                a_srv->clients[connections].resend_ack_buffer = ack_buf;    //freethis only after receiving next data

                sendto(a_srv->sd, ack_buf, len, 0, (struct sockaddr*)&a_srv->clients[connections].client_sd, sizeof(a_srv->clients[connections].client_sd));
                pthread_mutex_unlock(&a_srv->clients[connections].timer_mutex);

        } else if(msg->connid > 0 && msg->payload.len != 0){  // its the DAta , enqueue and send ACK
		// received something, so update timers and epochs
		pthread_mutex_lock(&a_srv->clients[msg->connid].timer_mutex);
                a_srv->clients[msg->connid].epoch_count = 0;
		pthread_mutex_unlock(&a_srv->clients[msg->connid].timer_mutex);

		// sequence number check for out of order data 
		if(msg->seqnum != a_srv->clients[msg->connid].expected_seq_num) {
			free(buf);
		        lspmessage__free_unpacked(msg,NULL);
			continue;
		}
		a_srv->clients[msg->connid].expected_seq_num++;
		if(a_srv->clients[msg->connid].expected_seq_num == 0)	//handle zero seq num
			a_srv->clients[msg->connid].expected_seq_num++;

		void* data_buf;
		data_buf = malloc(msg->payload.len);
		memcpy(data_buf, msg->payload.data, msg->payload.len);

		pthread_mutex_lock(&a_srv->read_q_mutex);
		enqueue(&a_srv->rq_head, data_buf, msg->payload.len, msg->connid, &a_srv->read_q_mutex);
		pthread_mutex_unlock(&a_srv->read_q_mutex);
		/* send ACK */
                void* ack_buf;
                int len;
                LSPMessage ack_msg = LSPMESSAGE__INIT;

                ack_msg.connid = msg->connid;
                ack_msg.seqnum = msg->seqnum;
                ack_msg.payload.data = NULL;
                ack_msg.payload.len = 0;
                len = lspmessage__get_packed_size(&ack_msg);
                ack_buf = malloc(len);
                lspmessage__pack(&ack_msg, ack_buf);

                pthread_mutex_lock(&a_srv->clients[msg->connid].timer_mutex);

                //a_srv->clients[msg->connid].last_received = (1000000*tv.tv_sec) + tv.tv_usec;
		//a_srv->clients[msg->connid].epoch_count = 0;
                a_srv->clients[msg->connid].resend_ack_length = len;
		free(a_srv->clients[msg->connid].resend_ack_buffer);	// freeing the ACK buffer
                a_srv->clients[msg->connid].resend_ack_buffer = ack_buf;    // free this only after receiving ACK

                sendto(a_srv->sd, ack_buf, len, 0, (struct sockaddr*)&a_srv->clients[msg->connid].client_sd, sizeof(a_srv->clients[msg->connid].client_sd));
                pthread_mutex_unlock(&a_srv->clients[msg->connid].timer_mutex);
        } else if(msg->connid > 0 && msg->payload.len == 0) { // its the valid ACK, set global state and free resources

		pthread_mutex_lock(&a_srv->clients[msg->connid].timer_mutex);
		
		a_srv->clients[msg->connid].allow_write = 1;
		a_srv->clients[msg->connid].epoch_count = 0;
        	//a_srv->clients[msg->connid].resend_length = 0;	// the next write can happen now
	        //free(a_srv->clients[msg->connid].resend_buffer);     		

		pthread_mutex_unlock(&a_srv->clients[msg->connid].timer_mutex);
        }

        free(buf);
	lspmessage__free_unpacked(msg,NULL);

	} // end of while


}

void* write_to_clients(void* thread_args) {
/*
        dequeue
        marshall
        sendto()
        set state in global
        Fail write until ACK is received for previous write
        return TRUE/FALSE
*/
        void* buf;
        int len;
        LSPMessage msg = LSPMESSAGE__INIT;

	lsp_server*	a_srv = (lsp_server*) thread_args;
	int		lth;
	void*		pld;
	uint32_t	conn_id;
	
	while(1) {
	pld = (uint8_t*) malloc(BUF_LEN);
	pthread_mutex_lock(&a_srv->write_q_mutex);
	if((lth=dequeue(&a_srv->wq_head, pld, &conn_id)) != 0) {


        	if(a_srv->clients[conn_id].allow_write == 0) {
			//printf("inside case\n");
                	enqueue(&a_srv->wq_head, pld, lth, conn_id, &a_srv->write_q_mutex);
			pthread_mutex_unlock(&a_srv->write_q_mutex);
			continue;
		}
		pthread_mutex_unlock(&a_srv->write_q_mutex);
	} else {
		pthread_mutex_unlock(&a_srv->write_q_mutex);
		free(pld);
		continue;
	}
		

        msg.connid = conn_id;
        msg.seqnum = a_srv->clients[conn_id].seq_num;                   //  managing sequence id
        a_srv->clients[conn_id].seq_num++;
	if(a_srv->clients[conn_id].seq_num == 0)			// handle zero sequence number
		a_srv->clients[conn_id].seq_num++;
        msg.payload.data = malloc(sizeof(uint8_t) * lth);
        msg.payload.len = lth;
        memcpy(msg.payload.data, pld, lth*sizeof(uint8_t));
        

        len = lspmessage__get_packed_size(&msg);
        buf = malloc(len);
        lspmessage__pack(&msg, buf);

        pthread_mutex_lock(&a_srv->clients[conn_id].timer_mutex);
	//freeing previous write
	a_srv->clients[conn_id].resend_length = 0;        // the next write can happen now
        free(a_srv->clients[conn_id].resend_buffer); 

        a_srv->clients[conn_id].allow_write = 0;

        a_srv->clients[conn_id].resend_length = len;
        a_srv->clients[conn_id].resend_buffer = buf;    //freethis only after receiving ACK     
        sendto(a_srv->sd, buf, len, 0, (struct sockaddr*)&a_srv->clients[conn_id].client_sd, sizeof(a_srv->clients[conn_id].client_sd));
        pthread_mutex_unlock(&a_srv->clients[conn_id].timer_mutex);


	free(pld);
        free(msg.payload.data);
	}


}

void* read_from_server(void* thread_args) {
	LSPMessage *msg;
        int msg_len;
        uint8_t* buf;
        struct sockaddr_in  server_sd;
        int server_addr_len;
        lsp_client* a_client = (lsp_client*) thread_args;


        while(1) {

        buf = (uint8_t*) malloc(BUF_LEN);
        server_addr_len = sizeof(server_sd);
        msg_len = recvfrom(a_client->sd, buf, BUF_LEN, 0, (struct sockaddr*)&server_sd, (socklen_t*)&server_addr_len);
	if(msg_len == -1 || msg_len == 0) {
		free(buf);
		continue;
	}
        msg = lspmessage__unpack(NULL, msg_len, buf);
        if(msg == NULL) {
                perror("unpack error");
                continue;
        }
	//drop packets according to the drop rate
	if(gen_random() < drop_rate){
	   free(buf);
       	   lspmessage__free_unpacked(msg,NULL);
	   continue;
  	}
        /*monitor for Data and ACK,     if DAta, enqueue and send ACK
                                        if ACK enable sending next data */

        if (msg->connid > 0 && msg->payload.len != 0) { //DATA, enqueue
		if(msg->seqnum != a_client->expected_seq_num) {
			free(buf);
		        lspmessage__free_unpacked(msg,NULL);
                        continue;
		}
                a_client->expected_seq_num++;
                if(a_client->expected_seq_num == 0)   //handle zero seq num
                        a_client->expected_seq_num++;

		void* data_buf;
                data_buf = malloc(msg->payload.len);
                memcpy(data_buf, msg->payload.data, msg->payload.len);

                pthread_mutex_lock(&a_client->read_q_mutex);
                enqueue(&a_client->rq_head, data_buf, msg->payload.len, msg->connid, &a_client->read_q_mutex);
                pthread_mutex_unlock(&a_client->read_q_mutex);

                /* send ACK */
                void* ack_buf;
                int len;
                LSPMessage ack_msg = LSPMESSAGE__INIT;

                ack_msg.connid = a_client->conn_id;
                ack_msg.seqnum = a_client->seq_num;;
                ack_msg.payload.data = NULL;
		ack_msg.payload.len = 0;
                len = lspmessage__get_packed_size(&ack_msg);
                ack_buf = malloc(len);
                lspmessage__pack(&ack_msg, ack_buf);


		pthread_mutex_lock(&a_client->timer_mutex);

		a_client->epoch_count = 0;
                a_client->resend_ack_length = len;
                free(a_client->resend_ack_buffer);    // freeing the ACK buffer
                a_client->resend_ack_buffer = ack_buf;    // free this only after receiving ACK

                sendto(a_client->sd, ack_buf, len, 0, (struct sockaddr*)&a_client->server_sd, sizeof(a_client->server_sd));
                pthread_mutex_unlock(&a_client->timer_mutex);



        } else if (msg->connid > 0 && msg->payload.len == 0) { //ACK for data sent by client, allow write

		pthread_mutex_lock(&a_client->timer_mutex);

		a_client->epoch_count = 0;
                a_client->allow_write = 1;
                //a_client->resend_length = 0;
                //free(a_client->resend_buffer);          

                pthread_mutex_unlock(&a_client->timer_mutex);

        }

        free(buf);
        lspmessage__free_unpacked(msg,NULL);

	} //end of while

}

void* write_to_server(void* thread_args) {

	void* buf;
        int len;
        LSPMessage msg = LSPMESSAGE__INIT;

        lsp_client*     a_client = (lsp_client*) thread_args;
        int             lth;
        void*           pld;
        uint32_t        conn_id;

        while(1) {

	pld = (uint8_t*) malloc(BUF_LEN);
        pthread_mutex_lock(&a_client->write_q_mutex);
        if((lth=dequeue(&a_client->wq_head, pld, &conn_id)) != 0) {


                if(a_client->allow_write == 0) {
                        //printf("inside case\n");
                        enqueue(&a_client->wq_head, pld, lth, conn_id, &a_client->write_q_mutex);
                        pthread_mutex_unlock(&a_client->write_q_mutex);
                        continue;
                }
                pthread_mutex_unlock(&a_client->write_q_mutex);
        } else {
                pthread_mutex_unlock(&a_client->write_q_mutex);
                free(pld);
                continue;
        }


        msg.connid = a_client->conn_id;
        msg.seqnum = a_client->seq_num;                   //  manage sequence id
        a_client->seq_num++;
        if(a_client->seq_num == 0)                        // handle zero sequence number
                a_client->seq_num++;

        msg.payload.data = malloc(sizeof(uint8_t) * lth);
        msg.payload.len = lth;
        memcpy(msg.payload.data, pld, lth*sizeof(uint8_t));


        len = lspmessage__get_packed_size(&msg);
        buf = malloc(len);
        lspmessage__pack(&msg, buf);

        pthread_mutex_lock(&a_client->timer_mutex);
	//free the previous write
	a_client->resend_length = 0;
        free(a_client->resend_buffer);
 
        a_client->allow_write = 0;
        a_client->resend_length = len;
        a_client->resend_buffer = buf;    //freethis only after receiving ACK     
        sendto(a_client->sd, buf, len, 0, (struct sockaddr*)&a_client->server_sd, sizeof(a_client->server_sd));
        pthread_mutex_unlock(&a_client->timer_mutex);


        free(pld);
        free(msg.payload.data);
        }


}

void* epoch_clientside(void* thread_args) {
//#if 0
	lsp_client*     a_client = (lsp_client*) thread_args;
        struct timeval tv, curr_tv, new_tv, select_tv;
        unsigned int    curr_time;

        while(1) {
                gettimeofday(&tv,NULL);

                if(a_client->conn_id != 0 && a_client->timer == 0) // valid new client
                        a_client->timer = (1000000*tv.tv_sec) + tv.tv_usec;
		select_tv.tv_sec = 0.5;
		select_tv.tv_usec = 0;
		if (select(0, NULL, NULL, NULL, &select_tv) == 0) {
		//printf("After sleep\n");

                        gettimeofday(&curr_tv,NULL);
                        curr_time = (1000000*curr_tv.tv_sec) + curr_tv.tv_usec;
                        if(a_client->conn_id != 0 && a_client->timer != 0) { //valid existing client

                                if(curr_time - a_client->timer >= (epoch_lth*1000000)) { // epoch timer fires,check outstanding jobs & resend if any

                                        gettimeofday(&new_tv,NULL);
                                        a_client->timer = (1000000*new_tv.tv_sec) + new_tv.tv_usec;

                                        pthread_mutex_lock(&a_client->timer_mutex);
                                        a_client->epoch_count++;
                                        pthread_mutex_unlock(&a_client->timer_mutex);

                                        if(a_client->epoch_count > epoch_cnt) { // nothing was received in epochlth*epochcnt time, close the connection
						pthread_mutex_lock(&a_client->timer_mutex);
                                                a_client->server_disconnected = 1;
                                                pthread_mutex_unlock(&a_client->timer_mutex);
                                                continue;
                                        }

                                        if(a_client->resend_ack_length != 0 ) { // resend ACK
					//printf("resending ACK \n");
                                        pthread_mutex_lock(&a_client->timer_mutex);
                                        sendto(a_client->sd, a_client->resend_ack_buffer, a_client->resend_ack_length, 0, (struct sockaddr*)&a_client->server_sd, sizeof(a_client->server_sd));
					pthread_mutex_unlock(&a_client->timer_mutex);
                                        }

                                        if(a_client->resend_length !=0 ) { //resend DATA
					//printf("resending DATA \n");
                                        pthread_mutex_lock(&a_client->timer_mutex);
                                        sendto(a_client->sd, a_client->resend_buffer, a_client->resend_length, 0, (struct sockaddr*)&a_client->server_sd, sizeof(a_client->server_sd));
					pthread_mutex_unlock(&a_client->timer_mutex);

                                        }

                                }

                        }
		}
        } // end of while loop
//#endif
}

/*
Variables used for epoch handling:
double epoch_lth = _EPOCH_LTH
int epoch_cnt = _EPOCH_CNT
*/
void* epoch_for_a_client(void* thread_args) {
//#if 0
	lsp_server*     a_srv = (lsp_server*) thread_args;
	int i;
        struct timeval tv, curr_tv, new_tv, select_tv;
	unsigned int	curr_time;

	while(1) {
        	gettimeofday(&tv,NULL);

		for(i=1;i<MAX_CLIENTS;i++) { //initialize initial timer values
			if(a_srv->clients[i].conn_id != 0 && a_srv->clients[i].timer == 0 && a_srv->clients[i].client_disconnected == 0) // valid new client
				a_srv->clients[i].timer = (1000000*tv.tv_sec) + tv.tv_usec;
		}
		select_tv.tv_sec = 0.5;
		select_tv.tv_usec = 0;
		if (select(0, NULL, NULL, NULL, &select_tv) == 0) {
		//printf("After sleep\n");
		for(i=1;i<MAX_CLIENTS;i++) { //client number starts from 1; 
			gettimeofday(&curr_tv,NULL);
			curr_time = (1000000*curr_tv.tv_sec) + curr_tv.tv_usec;
			if(a_srv->clients[i].conn_id != 0 && a_srv->clients[i].timer != 0 && a_srv->clients[i].client_disconnected == 0) { //valid existing client

				//printf("Inside timer event\n");
				if(curr_time - a_srv->clients[i].timer >= (epoch_lth*1000000)) { // epoch timer fires,check outstanding jobs & resend if any
					//printf("Inside epoch fire\n");

					gettimeofday(&new_tv,NULL);
					a_srv->clients[i].timer = (1000000*new_tv.tv_sec) + new_tv.tv_usec;

					pthread_mutex_lock(&a_srv->clients[i].timer_mutex);
			                a_srv->clients[i].epoch_count++;
                			pthread_mutex_unlock(&a_srv->clients[i].timer_mutex);

					if(a_srv->clients[i].epoch_count > epoch_cnt) { // nothing was received in epochlth*epochcnt time, close the connection
						pthread_mutex_lock(&a_srv->master_lock);
						a_srv->clients[i].client_disconnected = 1;
						pthread_mutex_unlock(&a_srv->master_lock);
						continue;
					}

					if(a_srv->clients[i].resend_ack_length != 0 ) { // resend ACK
					//printf("resending ACK \n");
					pthread_mutex_lock(&a_srv->clients[i].timer_mutex);
                			sendto(a_srv->sd, a_srv->clients[i].resend_ack_buffer, a_srv->clients[i].resend_ack_length, 0, (struct sockaddr*)&a_srv->clients[i].client_sd, sizeof(a_srv->clients[i].client_sd));
                			pthread_mutex_unlock(&a_srv->clients[i].timer_mutex);
					}

					if(a_srv->clients[i].resend_length !=0 ) { //resend DATA
					//printf("resending DATA \n");
					pthread_mutex_lock(&a_srv->clients[i].timer_mutex);
                			sendto(a_srv->sd, a_srv->clients[i].resend_buffer, a_srv->clients[i].resend_length, 0, (struct sockaddr*)&a_srv->clients[i].client_sd, sizeof(a_srv->clients[i].client_sd));
                			pthread_mutex_unlock(&a_srv->clients[i].timer_mutex);
	
					}

				}
					
			}
		} // end of for loop
		}
	} // end of while loop
//#endif
}

void stringTonum(char* str, int len, int64_t* val) {

        int i,j;

        i = len-1;
        j = 0;
        while(i>=0) {
                *val += ( (str[i]-97) * powl(26, j));
                j++;
                i--;
        }

}
void numTostring(char* str, int64_t num, int len) {
        int i,j;
        int res[12] = {0};


        i = len-1;
        while(i>=0) {
                res[i] = num % 26;
                num = num/26;
                i--;
        }
        for(j=0;j<len;j++) {
                sprintf(str+j, "%c", res[j]+97);
        }

}

int stringTohex(const char *str)
{
        int res=0;
        int i;
        char temp;
        int acc=0;

        for(i=0;i<2;i++) {
                temp = *str;
                str++;
                if( temp >= '0' && temp <= '9' )
                        acc = temp - '0';
                else if( temp >= 'a' && temp <= 'f' )
                        acc = 10 + temp - 'a';
                res = acc + res*16;  //shift left res by 4 and add acc
        }
        return res;
}

float gen_random()
{
	return rand()/(float)(RAND_MAX);
}
