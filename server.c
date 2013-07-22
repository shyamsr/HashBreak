#include "lsp.h"
#include "server.h"

/* format ./server port */

/*Round robin scheduler*/ 
int find_next_client(struct request* req, int32_t conn_id) {
	int i = 0;
	int found = 0;
	while(req[i].conn_id != conn_id && i<MAX_REQUESTS) i++;
	while(i<MAX_REQUESTS) {
		if(req[i].conn_id != 0 && req[i].pass_not_cracked==1) {
			found = 1;
			return i;
		}
		i++;
	}
	i=0;
	while(i<MAX_REQUESTS) {
		if(req[i].conn_id != 0 && req[i].pass_not_cracked==1 ) {
			return i;
		}
		i++;
	}
	return 0;
}
void leave(sig)
{
	run=0;
}

int enqueue_job(struct outstanding** q_head, struct worker* add) {

        struct outstanding* temp;

        if(*q_head == NULL ) { // head is NULL
                temp = (struct outstanding*)malloc(sizeof(struct outstanding));
                if(temp==NULL)
                        return 0;
                temp->conn_id = add->conn_id;
                strcpy(temp->hash, add->hash);
                strcpy(temp->lower, add->lower);
		strcpy(temp->upper, add->upper);
                temp->next = NULL;
                *q_head = temp;
		//printf("Enqueueing offline worker's job\n");
        } else {
                temp = *q_head;
                assert(temp!=NULL);
                while(temp->next != NULL)
                        temp = temp->next;

                temp->next = (struct outstanding*)malloc(sizeof(struct outstanding));
                if(temp->next==NULL) {
                        return 0;
                }
		temp->next->conn_id = add->conn_id;
                strcpy(temp->next->hash, add->hash);
                strcpy(temp->next->lower, add->lower);
                strcpy(temp->next->upper, add->upper);
                temp->next->next = NULL;
		//printf("Enqueueing offline worker's job\n");
        }
        //srs: handle failure case
        return 0;

}

int dequeue_job(struct outstanding** q_head, struct outstanding* ele) {
        struct outstanding* temp;
        if(*q_head != NULL) {
                assert(*q_head!=NULL);
		//printf("Dequeueing offline worker's job\n");
                temp = (*q_head)->next;

                ele->conn_id = (*q_head)->conn_id;
                strcpy(ele->hash, (*q_head)->hash);
                strcpy(ele->lower, (*q_head)->lower);
                strcpy(ele->upper, (*q_head)->upper);
                free(*q_head);
                *q_head = temp;
		return 1;
        }
        return 0;
}



int main(int argc, char** argv) 
{
	struct request req[MAX_REQUESTS];
	struct worker  work[MAX_WORKERS];
	uint8_t payload[BUF_LEN];
	uint32_t conn_id;
	int bytes_read;
	int64_t jobs = 0;
	int workers = 0;
	int requests = 0;
	int next_client = 0;
	(void)signal(SIGINT,leave);

	if(argc != 2) {
                printf("Please enter a valid command: ./server port\n");
                return -1;
        }

	memset(work, 0, sizeof(work));
	memset(req, 0, sizeof(req));
	lsp_server* server = lsp_server_create(atoi(argv[1]));

	while(run)
	{
		bytes_read = lsp_server_read(server, payload, &conn_id);

		if(bytes_read == 0 && conn_id == 0)
			continue;

		if(bytes_read == 0 && conn_id > 0) { // this client is offline, handle
			int i=0, j=0, found = 0;
			while(j<MAX_WORKERS) { //find worker
				if(work[j].conn_id == conn_id && work[j].busy == 1) { //add to outstanding job queue of the request
					enqueue_job(&req[work[j].req_index].rem_q_head, &work[j]);
					memset(&work[j], 0, sizeof(struct worker));
					found =1;	
					workers--;
					break;
				}
				j++;
			}
			if(found==1) continue;
			while(i<MAX_REQUESTS) { //find request client
				if(req[i].conn_id == conn_id) { //cleanup

					requests--;	
                                }
                                i++;
                        }
			continue;
			
		}

		if( strncmp((const char *)payload, "j", 1) == 0) {

			if(workers == MAX_WORKERS) {
				//send close worker
				continue;	
			}
			int i=0,j=0;
                        while(work[j].conn_id != 0 && j<MAX_WORKERS) j++;
			workers++;
			work[j].conn_id = conn_id;
			work[j].busy = 0;
			

			i = next_client;
                        if(req[i].pass_not_cracked==0)
                                i = find_next_client(req, req[i].conn_id);

			struct outstanding	process_this;
			char            	message[50];
			
			if(req[i].rem_q_head != NULL) {
			 if(dequeue_job(&req[i].rem_q_head, &process_this) == 1 && req[i].pass_not_cracked == 1) {  //dequeue success
				int64_t lower;
				int64_t upper;

				stringTonum(process_this.lower, req[i].len, &lower);
				stringTonum(process_this.upper, req[i].len, &upper);
				strcpy(work[j].hash, req[i].hash);
                                numTostring(work[j].lower, lower, req[i].len);
				numTostring(work[j].upper, upper, req[i].len);
                                work[j].req_index = i;

				sprintf(message, "%c", 'c');
                                sprintf(message+1, "%c", ' ');
                                strcat(message, req[i].hash);
                                sprintf(message+1+strlen(req[i].hash)+1, "%c", ' ');
                                strcat(message, work[j].lower);
                                sprintf(message+1+strlen(req[i].hash)+1+req[i].len+1, "%c", ' ');
                                strcat(message, work[j].upper);
                                lsp_server_write(server, message, strlen(message)+1, work[j].conn_id);
                                work[j].busy = 1;
				next_client = find_next_client(req, req[i].conn_id);
				continue;

			 }
			}

                        j=0;
                        while(work[j].busy == 0 && work[j].conn_id != 0 && j<MAX_WORKERS && req[i].next_job <= req[i].upper && req[i].pass_not_cracked==1) {
                                strcpy(work[j].hash, req[i].hash);
                                numTostring(work[j].lower, req[i].next_job, req[i].len);
				work[j].req_index = i;
                                if( (req[i].next_job + SPLIT-1) > req[i].upper) {
                                        numTostring(work[j].upper, req[i].upper, req[i].len);
                                        sprintf(message, "%c", 'c');
                                        sprintf(message+1, "%c", ' ');
                                        strcat(message, req[i].hash);
                                        sprintf(message+1+strlen(req[i].hash)+1, "%c", ' ');
                                        strcat(message, work[j].lower);
                                        sprintf(message+1+strlen(req[i].hash)+1+req[i].len+1, "%c", ' ');
                                        strcat(message, work[j].upper);
                                        lsp_server_write(server, message, strlen(message)+1, work[j].conn_id);
                                        //printf("sending next message %s\n", message);
                                        req[i].next_job  = req[i].upper+1;
                                        work[j].busy = 1;
                                        //job assignment is complete
                                } else {
                                        numTostring(work[j].upper, req[i].next_job + SPLIT-1, req[i].len);
                                        sprintf(message, "%c", 'c');
                                        sprintf(message+1, "%c", ' ');
                                        strcat(message, req[i].hash);
                                        sprintf(message+1+strlen(req[i].hash)+1, "%c", ' ');
                                        strcat(message, work[j].lower);
                                        sprintf(message+1+strlen(req[i].hash)+1+req[i].len+1, "%c", ' ');
                                        strcat(message, work[j].upper);
                                        lsp_server_write(server, message, strlen(message)+1, work[j].conn_id);
                        		//printf("found worker id %d assigning request to connid %d\n",j, req[i].conn_id);
                                        //printf("sending next message %s\n", message);
                                        req[i].next_job += SPLIT;
                                        work[j].busy = 1;
                                        }
                                j++;
                        }
                        next_client = find_next_client(req, req[i].conn_id);

		} else if(strncmp((const char *)payload, "c", 1) == 0) {  //crack request
			char hash[50];
			char lower[730], upper[730];
			int i=0, j=0;
			char* token = strtok((char*)payload, " ");
			char            message[1500];

			if(requests == MAX_REQUESTS) {
				//srs: handle maxx requests here
				continue;
			}

			token = strtok(NULL," ");
			while (token) {  //tokenize
				if(i==0)
    					strcpy(hash, token);
				if(i==1)
    					strcpy(lower, token);
				if(i==2)
    					strcpy(upper, token);
				
    				token = strtok(NULL, " ");
				i++;
			}
			
			if(strlen(lower) >= 12 /*|| workers == 0*/) {
				sprintf(message, "%c", 'x');
                	        lsp_server_write(server, message, strlen(message)+1, conn_id);
				continue;
			}
				 
			//start work division
			int64_t iter_lower = 0, iter_upper = 0;
			int64_t divisor = SPLIT;
			int64_t iter;
			stringTonum(upper, strlen(upper), &iter_upper);
			stringTonum(lower, strlen(lower), &iter_lower);
			iter = iter_upper - iter_lower;
			jobs += (iter / divisor) + 1;
			requests++;

			i=0;
			while(req[i].conn_id != 0 && i<MAX_REQUESTS) i++;

			//printf("recvd crack req. hash %s assigned index %d connid %d\n", hash, i, conn_id);

			memset(&req[i], 0, sizeof(struct request));			
			req[i].conn_id = conn_id;
			strcpy(req[i].hash, hash);
			req[i].upper = iter_upper;
			req[i].next_job = 0;
			req[i].len = strlen(upper);
			req[i].pass_not_cracked = 1;
			req[i].rem_q_head = NULL;

			j=0;
			while(work[j].busy == 0 && work[j].conn_id != 0 && j<MAX_WORKERS && req[i].next_job <= req[i].upper && req[i].pass_not_cracked==1) {
				strcpy(work[j].hash, hash);
				work[j].req_index = i;
				numTostring(work[j].lower, req[i].next_job, req[i].len);
				if( (req[i].next_job + SPLIT-1) > req[i].upper) {
					numTostring(work[j].upper, req[i].upper, req[i].len);
					sprintf(message, "%c", 'c');
				        sprintf(message+1, "%c", ' ');
				        strcat(message, req[i].hash);
				        sprintf(message+1+strlen(req[i].hash)+1, "%c", ' ');
				        strcat(message, work[j].lower);
				        sprintf(message+1+strlen(req[i].hash)+1+strlen(lower)+1, "%c", ' ');
				        strcat(message, work[j].upper);
					lsp_server_write(server, message, strlen(message)+1, work[j].conn_id);
					req[i].next_job  = req[i].upper+1;
					work[j].busy = 1;
					//job assignment is complete
				} else {
					numTostring(work[j].upper, req[i].next_job + SPLIT-1, req[i].len);
					sprintf(message, "%c", 'c');
                                        sprintf(message+1, "%c", ' ');
                                        strcat(message, req[i].hash);
                                        sprintf(message+1+strlen(req[i].hash)+1, "%c", ' ');
                                        strcat(message, work[j].lower);
                                        sprintf(message+1+strlen(req[i].hash)+1+strlen(lower)+1, "%c", ' ');
                                        strcat(message, work[j].upper);
                                        lsp_server_write(server, message, strlen(message)+1, work[j].conn_id);
					req[i].next_job += SPLIT;
					work[j].busy = 1;
				}
				j++;
			}
			next_client = find_next_client(req, req[i].conn_id);;
				
			
		} else if(strncmp((const char*)payload, "f", 1) == 0) {
			char    message[50];
			char 	pass[15];
                        int 	i=0, j=0;
			strcpy((char*)message,(const char*)payload);
                        char* token = strtok((char*)payload, " ");

                        token = strtok(NULL," ");
                        while (token) {  //tokenize
                                strcpy(pass, token);
                                token = strtok(NULL, " ");
                        }
			j=0;
                        while(work[j].conn_id != conn_id && j<MAX_WORKERS) j++;	
			i=0;
			while(i<MAX_REQUESTS) {
				if(strcmp(work[j].hash, req[i].hash) == 0) {
				//if(work[j].req_index == i) {
					requests--;
					//printf("Pass found: %s work hash %s req hash %s\n", pass, work[j].hash, req[i].hash);
					req[i].pass_not_cracked = 0;
					lsp_server_write(server, message, strlen(message)+1, req[i].conn_id);
					memset(&req[i], 0, sizeof(struct request));
					req[i].rem_q_head = NULL;
					break;
				}
				i++;
			}
			work[j].busy = 0;
			memset(message, 0, sizeof(message));

			i = next_client;
                        if(req[i].pass_not_cracked==0)
                                i = find_next_client(req, req[i].conn_id);

                        j=0;
                        while(work[j].busy == 0 && work[j].conn_id != 0 && j<MAX_WORKERS && req[i].next_job <= req[i].upper && req[i].pass_not_cracked==1) {
                                strcpy(work[j].hash, req[i].hash);
				work[j].req_index = i;
                                numTostring(work[j].lower, req[i].next_job, req[i].len);
                                if( (req[i].next_job + SPLIT-1) > req[i].upper) {
                                        numTostring(work[j].upper, req[i].upper, req[i].len);
                                        sprintf(message, "%c", 'c');
                                        sprintf(message+1, "%c", ' ');
                                        strcat(message, req[i].hash);
                                        sprintf(message+1+strlen(req[i].hash)+1, "%c", ' ');
                                        strcat(message, work[j].lower);
                                        sprintf(message+1+strlen(req[i].hash)+1+req[i].len+1, "%c", ' ');
                                        strcat(message, work[j].upper);
                                        lsp_server_write(server, message, strlen(message)+1, work[j].conn_id);
                                        //printf("sending next message %s\n", message);
                                        req[i].next_job  = req[i].upper+1;
                                        work[j].busy = 1;
                                        //job assignment is complete
                                } else {
                                        numTostring(work[j].upper, req[i].next_job + SPLIT-1, req[i].len);
                                        sprintf(message, "%c", 'c');
                                        sprintf(message+1, "%c", ' ');
                                        strcat(message, req[i].hash);
                                        sprintf(message+1+strlen(req[i].hash)+1, "%c", ' ');
                                        strcat(message, work[j].lower);
                                        sprintf(message+1+strlen(req[i].hash)+1+req[i].len+1, "%c", ' ');
                                        strcat(message, work[j].upper);
                                        lsp_server_write(server, message, strlen(message)+1, work[j].conn_id);
                                        //printf("sending next message %s\n", message);
                                        req[i].next_job += SPLIT;
                                        work[j].busy = 1;
					}
                                j++;
                        }
                        next_client = find_next_client(req, req[i].conn_id);

		} else if(strncmp((const char*)payload, "x", 1) == 0) {
			//printf("password not found\n");
			char    message[50];
			int i,j,k,count=0;
			

                        j=0;
                        while(work[j].conn_id != conn_id && j<MAX_WORKERS) j++; // find worker index
                        i=0;
                        while(strcmp(work[j].hash, req[i].hash)!=0 && i<MAX_REQUESTS) i++;  //find request client index
			k=0;
			while(work[k].conn_id == conn_id && k<MAX_WORKERS) {
				count++;
				k++;
			}
			
			if(req[i].next_job > req[i].upper && i<MAX_REQUESTS && count == 1) { //reached upper limit of checking, but check for outstanding jobs here.
				/* if outstanding job is found, then assign that job to this worker */
				struct outstanding      process_this;
                        	char                    message[50];

                        	 if(dequeue_job(&req[i].rem_q_head, &process_this) == 1 && req[i].pass_not_cracked == 1) {  //dequeue success
	                                int64_t lower;
        	                        int64_t upper;
	
        	                        stringTonum(process_this.lower, req[i].len, &lower);
                	                stringTonum(process_this.upper, req[i].len, &upper);
                        	        strcpy(work[j].hash, req[i].hash);
                                	numTostring(work[j].lower, lower, req[i].len);
	                                numTostring(work[j].upper, upper, req[i].len);
        	                        work[j].req_index = i;
	
        	                        sprintf(message, "%c", 'c');
                	                sprintf(message+1, "%c", ' ');
                        	        strcat(message, req[i].hash);
                                	sprintf(message+1+strlen(req[i].hash)+1, "%c", ' ');
	                                strcat(message, work[j].lower);
        	                        sprintf(message+1+strlen(req[i].hash)+1+req[i].len+1, "%c", ' ');
                	                strcat(message, work[j].upper);
                        	        lsp_server_write(server, message, strlen(message)+1, work[j].conn_id);
	                                work[j].busy = 1;
        	                        next_client = find_next_client(req, req[i].conn_id);
                	                continue;

                        	 }
				


				/*pass not found and reached limit case*/
				requests--;
				sprintf(message, "%c", 'x');
	                        req[i].pass_not_cracked = 0;
        	                work[j].busy = 0;
				//printf("Not Found\n");
                	        lsp_server_write(server, message, strlen(message)+1, req[i].conn_id);
                        	memset(&req[i], 0, sizeof(struct request));
			}

			memset(message, 0, sizeof(message));

			/*end of sending not found message*/

			j=0;
                        while(work[j].conn_id != conn_id && j<MAX_WORKERS) j++;
                        work[j].busy = 0;
			i = next_client;
			if(req[i].pass_not_cracked==0)
				i = find_next_client(req, req[i].conn_id);

			j=0;
			while(work[j].busy == 0 && work[j].conn_id != 0 && j<MAX_WORKERS && req[i].next_job <= req[i].upper && req[i].pass_not_cracked==1) {
                                strcpy(work[j].hash, req[i].hash);
				work[j].req_index = i;
                                numTostring(work[j].lower, req[i].next_job, req[i].len);
                                if( (req[i].next_job + SPLIT-1) > req[i].upper) {
                                        numTostring(work[j].upper, req[i].upper, req[i].len);
                                        sprintf(message, "%c", 'c');
                                        sprintf(message+1, "%c", ' ');
                                        strcat(message, req[i].hash);
                                        sprintf(message+1+strlen(req[i].hash)+1, "%c", ' ');
                                        strcat(message, work[j].lower);
                                        sprintf(message+1+strlen(req[i].hash)+1+req[i].len+1, "%c", ' ');
                                        strcat(message, work[j].upper);
                                        lsp_server_write(server, message, strlen(message)+1, work[j].conn_id);
					//printf("sending next message %s\n", message);
                                        req[i].next_job  = req[i].upper+1;
                                	work[j].busy = 1;
                                        //job assignment is complete
                                } else {
                                        numTostring(work[j].upper, req[i].next_job + SPLIT-1, req[i].len);
                                        sprintf(message, "%c", 'c');
                                        sprintf(message+1, "%c", ' ');
                                        strcat(message, req[i].hash);
                                        sprintf(message+1+strlen(req[i].hash)+1, "%c", ' ');
                                        strcat(message, work[j].lower);
                                        sprintf(message+1+strlen(req[i].hash)+1+req[i].len+1, "%c", ' ');
                                        strcat(message, work[j].upper);
                                        lsp_server_write(server, message, strlen(message)+1, work[j].conn_id);
					//printf("sending next message %s\n", message);
                                        req[i].next_job += SPLIT;
                                	work[j].busy = 1;
                                }
				j++;
                        }
			next_client = find_next_client(req, req[i].conn_id);


		} else { //invalid message
			continue;
		}


	}
	lsp_server_close(server,conn_id);
	
	return 0;
}
