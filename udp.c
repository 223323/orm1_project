#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <math.h>


fd_set fd;
enum epkt {
	pkt_ack=5,
	pkt_data
};

struct pkt {
	enum epkt type;
	int seq_num;
	int data_size;
};
void recv_data(int sock, int *length,  char** bytes);
void send_data(int sock, int length, int frame_size, char* bytes, struct sockaddr* addr);

int min(int a, int b) {
	if(a < b) return a;
	else return b;
}

#define PORT 27016
#define FRAME_SIZE 100

int main(int argc, char *argv[]) {
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;

	enum emode {
		mode_none,
		mode_client,
		mode_server
	} mode = mode_none;
	
	struct sockaddr_in addr2;
	addr2.sin_family = AF_INET;
	
	char file[100];
	strcpy(file, "file");
	FD_ZERO(&fd);
	FD_SET(sock, &fd);
	int i;
	for(i=1; i < argc; i++) {
		if(argv[i][0] == '-') {
			if(argv[i][1] == 'h') {
				// host
				printf("host mode\n");
				mode = mode_server;
				// default port 27015
				addr.sin_port = htons(PORT);
			} else if(argv[i][1] == 'c') {
				// connect
				printf("connect mode\n");
				mode = mode_client;
				// sledeca 2 argumenta su: ip port
				printf("ip port: %s:%s\n", argv[i+1], argv[i+2]);
				addr2.sin_addr.s_addr = inet_addr(argv[i+1]);
				addr2.sin_port = htons(atoi(argv[i+2]));
				i+=2;
				addr.sin_port = htons(PORT);
			} else if(argv[i][1] == 'f') {
				strcpy(file, argv[i+1]);
				i++;
			} else if(argv[i][1] == 'p') {
				addr.sin_port = htons( atoi(argv[i+1]) );
				i++;
			}

		}
	}
	if( bind(sock, (struct sockaddr*)&addr, sizeof(struct sockaddr)) == -1 ) {
		printf("bind error\n");
		return -1;
	}

	if(mode == mode_client) {
		FILE* f = fopen(file, "rb");
		fseek(f, 0, SEEK_END);
		int length = ftell(f);
		fseek(f, 0, SEEK_SET);
		char *data = (char*)malloc( length );
		fread(data, 1, length, f);
		fclose(f);
		// printf("sending file with data: \n%s\n", data);
		send_data(sock, length, FRAME_SIZE, data, (struct sockaddr*)&addr2);
	} else if(mode == mode_server) {
		printf("listening for data...\n");
		char* data;
		int length;
		recv_data(sock, &length, &data);
		FILE* f = fopen(file, "w");
		if(f) {
			fwrite(data, 1, length, f);
			free(data);
		}
	}

	return 0;
}


int wait_for_ack(int sock) {
	struct timeval timeout;
	timeout.tv_sec = 10;
	timeout.tv_usec = 0;
	struct pkt paket;
	struct sockaddr addr;
	socklen_t socklen = sizeof(struct sockaddr);
	int r;
	if((r=select(sock+1, &fd, 0, 0, &timeout)) >= 0) {
		// received ack?
		recvfrom(sock, (char*)&paket, sizeof(struct pkt), 0, &addr, &socklen);
		if(paket.type == pkt_ack) {
			return paket.seq_num;
		} else {
			// not ack
			return -2;
		}
	}
	return -2;
}

void send_ack(int sock, int seq_num, struct sockaddr* addr) {
	socklen_t socklen = sizeof(struct sockaddr);
	struct pkt paket;
	paket.type = pkt_ack;
	paket.seq_num = seq_num;
	paket.data_size = 0;
	sendto(sock, &paket, sizeof(struct pkt), 0, addr, socklen);
}

void dump_packet(struct pkt* paket, int print_data) {
	printf("packet type = %d, seq_num = %d, data_size = %d\n", paket->type, paket->seq_num, paket->data_size);
	if(print_data) {
		int i;
		char *bytes = (char*)(paket+1);
		for(i=0; i < paket->data_size; i++) {
			printf("%X ", bytes[i]);
		}
		printf("\n");
	}
}

void send_data(int sock, int length, int frame_size, char* bytes, struct sockaddr* addr) {
	int i;
	int transfer;
	socklen_t socklen = sizeof(struct sockaddr);
	int pkt_header_size = sizeof(struct pkt);
	int pkt_seq = 0;
	char *pkt_buf = (char*)malloc( frame_size + pkt_header_size );
	struct pkt *paket = (struct pkt*)pkt_buf;
	int r;
	do {
		// send 1 pkt for total size
		paket->type = pkt_data;
		paket->data_size = sizeof(int);
		paket->seq_num = -1;
		*((int*)(pkt_buf+pkt_header_size)) = length;
		// dump_packet(paket, 1);
		sendto(sock, pkt_buf, pkt_header_size+paket->data_size, 0, addr, socklen);
	} while( wait_for_ack(sock) != -1 );
	// printf("sending rest of data\n");
	struct sockaddr_in addr2;
	for(i=0; i < length; ) {
		transfer = min(frame_size, length-i);
		paket->type = pkt_data;
		paket->seq_num = pkt_seq;
		paket->data_size = transfer;
		memcpy(pkt_buf+pkt_header_size, bytes+i, transfer);
		sendto(sock, pkt_buf, transfer+pkt_header_size, 0, addr, socklen);
		printf("[%d] sent pkt len %d, seq %d\n", i, transfer, pkt_seq);
		if((r=wait_for_ack(sock)) == pkt_seq) {
			pkt_seq++;
			i+=transfer;
			printf("received ack num %d\n", pkt_seq-1);
		} else {
			printf("wrong ack %d %d\n", r, pkt_seq);
		}
		
	}
	free(pkt_buf);
}


void recv_data(int sock, int *length,  char** bytes) {
	socklen_t socklen = sizeof(struct sockaddr);
	int pkt_header_size = sizeof(struct pkt);
	int pkt_seq = 0;
	char *pkt_buf = (char*)malloc( pkt_header_size+4 );
	struct pkt *paket = (struct pkt*)pkt_buf;
	char* data;
	int total;
	struct sockaddr addr2;
	
	// recv 1 pkt for total size
	recvfrom(sock, pkt_buf, pkt_header_size+4, 0, (struct sockaddr*)&addr2, &socklen);
	if( paket->type == pkt_data ) {
		total = paket->data_size;
		// dump_packet(paket, 1);
		if(paket->data_size == sizeof(int) && paket->seq_num == -1) {
			total = *((int*)(pkt_buf+pkt_header_size));
			printf("need to allocate %d bytes\n", total);
			*bytes = (char*)malloc( total );
			*length = total;
			send_ack(sock, paket->seq_num, &addr2);
		}
	}
	
	printf("waiting for data\n");
	int i;
	int data_received=0;
	for(i = 0; i < total; i+=data_received) {
		int r = recvfrom(sock, pkt_buf, pkt_header_size+total, 0, (struct sockaddr*)&addr2, &socklen);
		if(r > 0 && paket->type == pkt_data && paket->seq_num >= 0) {
			data_received = paket->data_size;
			// printf("waiting for: %d\n", paket->data_size);
			// recvfrom(sock, *bytes+i, paket->data_size, 0, (struct sockaddr*)&addr2, &socklen);
			memcpy(*bytes+i, pkt_buf+pkt_header_size, paket->data_size);
			printf("received seq %d pkt\n", paket->seq_num);
			send_ack(sock, paket->seq_num, &addr2);
		} else {
			printf("fail pkt: %d\n", paket->type);
			exit(-1);
		}
	}

}
