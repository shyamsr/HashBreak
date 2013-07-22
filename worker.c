#include <stdio.h> 
#include <string.h> 
#include <openssl/sha.h>  
#include <math.h>
#include <sys/types.h>
#include <ctype.h>
#include "lsp.h"

sig_atomic_t run = 1;
void leave(sig)
{
	run = 0;
}
/* Format: ./worker host:port */
int main(int argc, char** argv) {     

        int             j, i=0;
        char            host[256];
        char            port_str[6];
        int             port;
	char		message[30];
	uint8_t         payload[50];
	char 	        check[15];
 	(void)signal(SIGINT,leave); 	

        if(argc != 2) {
                printf("Please enter a valid command: ./worker host:port\n");
                return -1;
        }

        while(argv[1][i] != ':') {
                if(i >=  strlen(argv[1])) {
                	printf("Please enter a valid command: ./worker host:port\n");
                        return -1;
                }
                sprintf(&host[i], "%c", argv[1][i]);
                i++;
        }
        i++;
        if(i >=  strlen(argv[1])) {
                printf("Please enter a valid command: ./worker host:port\n");
                return -1;
        } else if (argv[1][i] == ' ') {
                printf("Please enter a valid command: ./worker host:port\n");
                return -1;
        } else {
                j=0;
                while(i <=  strlen(argv[1])) {
                        sprintf(&port_str[j], "%c", argv[1][i]);
                        i++;
                        j++;
                }
        }
	
	port = atoi(port_str);

        lsp_client* work_client = lsp_client_create(host, port);

	if(work_client == NULL) {
		//printf("Not Found\n");
		exit(1);
	}

	sprintf(message, "%c", 'j');
	lsp_client_write(work_client, (void *) message, strlen(message)+1);

        while(run) {
                int bytes_read = lsp_client_read(work_client, payload);
                if(bytes_read == 0)
                        continue;

		if(bytes_read == -1) {
			lsp_client_close(work_client);
			exit(1);
		}

		if(strncmp((const char*)payload, "c", 1) == 0) {
                        char hash[50], temp[21], hash_actual[21];
                        char lower[15], upper[15];
                        int i=0;
			int pass_found = 0;
                        char* token = strtok((char* )payload, " ");

                        token = strtok(NULL," ");
                        while (token) {  //tokenize
                                if(i==0)
                                        strcpy((char* )hash, token);
                                if(i==1)
                                        strcpy(lower, token);
                                if(i==2)
                                        strcpy(upper, token);

                                token = strtok(NULL, " ");
                                i++;
                        }
			const char *temp_convert = hash;
    			for( i=0; i<20; i++ )
    			{	
        			hash_actual[i] = stringTohex(temp_convert);
        			temp_convert += 2;
    			}
			hash_actual[20] = '\0';

			//printf("Range %s %s %s %u\n", hash, lower, upper, strlen(upper));
			int64_t upper_k = 0;
			int64_t  k = 0;
			stringTonum(upper, strlen(upper), &upper_k);
			stringTonum(lower, strlen(lower), &k);
			//printf("lower %lld upper %llu\n", k, upper_k);
			for(; k<=upper_k;k++) {
				numTostring(check, k, strlen(lower));
				SHA1((unsigned char *)check, strlen(check),(unsigned char *)temp);
				temp[20] = '\0';
				if(strcmp(temp, hash_actual) == 0) {
					pass_found = 1;
					break;
				}
			}
			memset(message, 0, sizeof(message));
			if(pass_found == 0) {
				sprintf(message, "%c", 'x');
				lsp_client_write(work_client, (void *) message, strlen(message)+1);
			} else {
				sprintf(message, "%c", 'f');
                                sprintf(message+1, "%c", ' ');
				strcat(message, check);
				lsp_client_write(work_client, (void *) message, strlen(message)+1);	
			}
		}

        }

	lsp_client_close(work_client);
	return 0; 
}
