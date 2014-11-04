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

/* dns_question struct does not include qname, whose length is variable */
struct dns_question {
	unsigned short qtype;
	unsigned short qclass;
};

/* dns_answer struct does not include name and rdata, whose lengths are variable */
struct dns_answer {
	unsigned short type;
	unsigned short answer_class;
	unsigned int ttl;
	unsigned short rdlength;
};

void name_resolver(unsigned char *data, unsigned char *name, char hostname[HOSTNAME_MAX], int current_len);
int name_len_resolver(unsigned char *data, unsigned char *name);
void dns_packet_parser(unsigned char *data, int size, char hostname[HOSTNAME_MAX], char ip_addr[IP_LEN]);
