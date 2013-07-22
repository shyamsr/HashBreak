#include "lsp.h"

/* Format: ./request host:port hash len */
int main(int argc, char** argv) 
{

	int 		j, i=0;
	char 		host[256];
	char 		port_str[6];
	int 		port;
	char 		message[1500];
	uint8_t		message_from_server[50];
	char		lower[730], upper[730];
	int		len;
	
	if(argc != 4) {
		printf("Please enter a valid command: ./request host:port hash len\n");
                return -1;
	}
	
	while(argv[1][i] != ':') {	
		if(i >=  strlen(argv[1])) {
			printf("Please enter a valid command: ./request host:port hash len\n");
			return -1;
		}
		sprintf(&host[i], "%c", argv[1][i]);
		i++;
	}
	i++;
	if(i >=  strlen(argv[1])) {
        	printf("Please enter a valid command: ./request host:port hash len\n");
        	return -1;
        } else if (argv[1][i] == ' ') { 
		printf("Please enter a valid command: ./request host:port hash len\n");
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
	len = atoi(argv[3]);
	if(len > 730) { // Data payload (<c hash lower upper> can be max 1500 bytes. so len can be upto 730 
		printf("Not Found\n");
		exit(1);
	}

	lsp_client* req_client = lsp_client_create(host, port);
	
	if(req_client == NULL) {
		printf("Cannot create client\n");
		exit(1);
	}

	for(i=0;i<len;i++) {
		sprintf(lower+i, "%c", 'a');
		sprintf(upper+i, "%c", 'z');
	}
	sprintf(message, "%c", 'c');
	sprintf(message+1, "%c", ' ');
	strcat(message, argv[2]);
	sprintf(message+1+strlen(argv[2])+1, "%c", ' ');	
	strcat(message, lower);
	sprintf(message+1+strlen(argv[2])+1+strlen(lower)+1, "%c", ' ');	
	strcat(message, upper);

	lsp_client_write(req_client, (void *) message, strlen(message)+1);

	while(1) {
		int bytes_read = lsp_client_read(req_client, message_from_server);
        	if(bytes_read == 0) {
                        continue;
		} else if(bytes_read == -1) {
                        lsp_client_close(req_client);
			printf("Not Found\n");
                        exit(1);
                } else {
			break;
		}
	}
	if(strncmp((const char *)message_from_server, "x", 1) == 0) {
		printf("Not Found\n");
	
	} else {
	char    pass[15];
        char* token = strtok((char *)message_from_server, " ");

        token = strtok(NULL," ");
        while (token) {  //tokenize
        	strcpy(pass, token);
                token = strtok(NULL, " ");
        }

	printf("Found: %s\n", pass);
	}
	lsp_client_close(req_client);

	return 0;
}
