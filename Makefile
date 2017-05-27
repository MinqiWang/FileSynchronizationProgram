PORT = 51012
CFLAGS = -DPORT=$(PORT) -g -Wall -std=gnu99
HEADER = hash.h ftree.h

all: rcopy_client rcopy_server
	
rcopy_client: rcopy_client.o ftree.o hash_functions.o
	gcc ${CFLAGS} $^ -o $@
rcopy_server: rcopy_server.o ftree.o hash_functions.o
	gcc ${CFLAGS} $^ -o $@
%.o: %.c ${HEADER}
	gcc ${CFLAGS} -c $<
clean:
	rm *.o rcopy_client rcopy_server

