CC = gcc -Wall

TARGET =  request server worker

CFLAGS +=  -g -O0 -I/opt/local/include -pthread -lssl -lm
LDFLAGS += -g -lprotobuf-c -L/opt/local/lib

all:	$(TARGET)

$(TARGET): utils.o lsp.o lspmessage.pb-c.o

%.o:	%.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f *.o 
	rm -f $(TARGET)

