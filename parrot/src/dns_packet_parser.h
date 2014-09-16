#include <stdio.h>
#include <netinet/in.h>
#include <string.h>

#define HOSTNAME_MAX 255
#define IP_LEN 50

struct dns_header {
	unsigned short id;
	unsigned short flags;
	unsigned short qdcount;
	unsigned short ancount;
	unsigned short nscount;
	unsigned short arcount;
};

struct dns_question {
	unsigned short qtype;
	unsigned short qclass;
};

struct dns_answer {
	unsigned short type;
	unsigned short answer_class;
	unsigned int ttl;
	unsigned short rdlength;
};

void dns_packet_parser(unsigned char *data, int size, char hostname[HOSTNAME_MAX], char ip_addr[IP_LEN]);
int qname_resolver(unsigned char *qname, char hostname[HOSTNAME_MAX]);
int answer_name_resolver(unsigned char *name);
