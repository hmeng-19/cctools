#include "dns_packet_parser.h"

extern FILE *netlist_file;
extern struct hash_table *netlist_table;

int qname_resolver(unsigned char *qname, char hostname[HOSTNAME_MAX]) {
	int len, sub_len;
	len = 0;
	sub_len = (unsigned int)qname[len];
	strcpy(hostname, "");
	while(qname[len] != 0) {
		strncat(hostname, (char *)qname + len + 1, sub_len);
		strncat(hostname, ".", 1);
		len += (sub_len + 1);
		sub_len = (unsigned int)qname[len];
	}
	len --;
	hostname[len] = '\0';
//	fprintf(netlist_file, "hostname: %s\n", hostname);
	return len;
}

int answer_name_resolver(unsigned char *data, unsigned char *name, char cname_alias[HOSTNAME_MAX], int cname_flag) {
			;
	int len, sub_len;
	len = 0;
	strcpy(cname_alias, "");
	if(((name[0] >> 6) & 0x03) == 0x03) {
		if(cname_flag == 1) {
			int position;
			unsigned short offset = ntohs((unsigned short)((name[0] & 0x3f) * 256)) + ntohs((unsigned short)name[1]);
			position = ntohs(offset) & 0x3fff;
			qname_resolver(data + position, cname_alias);
		}
		return 2;
	}
	char sub_str[HOSTNAME_MAX];
	sub_len = (unsigned int)name[len];
	while(name[len] != 0) {
		if(((name[len] >> 6) & 0x03) == 0x03) {
			if(cname_flag == 1) {
				int position;
				unsigned short offset = ntohs((unsigned short)((name[len] & 0x3f) * 256)) + ntohs((unsigned short)name[len+1]);
				position = ntohs(offset) & 0x3fff;
				qname_resolver(data + position, sub_str);
				strcat(cname_alias, sub_str);
			}
			return len + 2;
		}
		if(cname_flag == 1) {
			strncat(cname_alias, (char *)name + len + 1, sub_len);
			strncat(cname_alias, ".", 1);
		}
		len += (sub_len + 1);
		sub_len = (unsigned int)name[len];
	}
	len --;
	cname_alias[len] = '\0';
	if(cname_flag == 1)
		fprintf(netlist_file, "cname_alias: %s\n", cname_alias);
	return len;
}

void dns_packet_parser(unsigned char *data, int size, char hostname[HOSTNAME_MAX], char ip_addr[IP_LEN], char cname_alias[HOSTNAME_MAX]) {
	hostname[0] = '\0';
	ip_addr[0] = '\0';
	cname_alias[0] = '\0';
	struct dns_header *dns_h = (struct dns_header *)data;

	//parse dns_header
//	fprintf(netlist_file, "DNS_Header:\n");
//	fprintf(netlist_file, "id: %d\n", ntohs(dns_h->id));

	unsigned short flags;
	flags = ntohs(dns_h->flags);

//	fprintf(netlist_file, "qr: %d\n", ((flags >> 15) & 0x0001));
//	fprintf(netlist_file, "opcode: %d\n", ((flags >> 11) & 0x000f));
//	fprintf(netlist_file, "aa: %d\n", ((flags >> 10) & 0x0001));
//	fprintf(netlist_file, "tc: %d\n", ((flags >> 9) & 0x0001));
//	fprintf(netlist_file, "rd: %d\n", ((flags >> 8) & 0x0001));
//	fprintf(netlist_file, "ra: %d\n", ((flags >> 7) & 0x0001));
//	fprintf(netlist_file, "rcode: %d\n", (flags & 0x000f));
//	fprintf(netlist_file, "qdcount: %d\n", ntohs(dns_h->qdcount));
//	fprintf(netlist_file, "ancount: %d\n", ntohs(dns_h->ancount));
//	fprintf(netlist_file, "nscount: %d\n", ntohs(dns_h->nscount));
//	fprintf(netlist_file, "arcount: %d\n", ntohs(dns_h->arcount));

	//parse dns_qeustion
	unsigned char *qname = data + 12;
	int qname_len;
	if(((flags >> 11) & 0x000f) == 0 && ntohs(dns_h->qdcount) == 1 && ntohs(dns_h->arcount) == 0) {
		qname_len = qname_resolver(qname, hostname);
//		struct dns_question *dns_q = (struct dns_question *) (qname + qname_len + 2);
//		fprintf(netlist_file, "qtype: %d\n", ntohs(dns_q->qtype));
//		fprintf(netlist_file, "qclass: %d\n", ntohs(dns_q->qclass));

		//parse dns_answer
		unsigned char *rdata;
		if(((flags >> 15) & 0x0001) == 1) {
			unsigned char *name = qname + qname_len + 2 + 4;
			struct dns_answer *dns_a;
			int name_len;
			name_len = answer_name_resolver(data, name, cname_alias, 0);
			dns_a = (struct dns_answer *)(name + name_len);
//			fprintf(netlist_file, "an_type: %d\n", ntohs(dns_a->type));
//			fprintf(netlist_file, "an_class: %d\n", ntohs(dns_a->answer_class));
//			fprintf(netlist_file, "an_len: %d\n", ntohs(dns_a->rdlength));
			int i;
			if((ntohs(dns_a->type) == 1 || ntohs(dns_a->type) == 5) && ntohs(dns_a->answer_class) == 1 && (ntohs(dns_a->rdlength)) > 0) {
				rdata = (unsigned char *) (name + name_len + 10);
				if(ntohs(dns_a->type) == 1) {
					char buf[4];
					strcpy(ip_addr, "");
					for(i = 0 ; i < (ntohs(dns_a->rdlength)); i++) {
//						fprintf(netlist_file,"%u.",rdata[i]); //if its a number or alphabet
						snprintf(buf, sizeof(buf), "%u", rdata[i]);
						strcat(ip_addr, buf);
						if(i < (ntohs(dns_a->rdlength) - 1))
							strcat(ip_addr, ".");
					}
				}
				if(ntohs(dns_a->type) == 5) {
/*
					for(i = 0 ; i < (ntohs(dns_a->rdlength)); i++) {
						fprintf(netlist_file," %02X",(unsigned int)rdata[i]);
					}
					fprintf(netlist_file, "\n");
					for(i = 0 ; i < (ntohs(dns_a->rdlength)); i++) {
						fprintf(netlist_file,"%c",(unsigned char)rdata[i]);
					}
					fprintf(netlist_file, "\n");
*/
//					int answer_len;
					answer_name_resolver(data, rdata, cname_alias, 1);
//					fprintf(netlist_file, "answer_len: %d; alias: %s\n", answer_len, cname_alias);
				}
//				fprintf(netlist_file, "hostname: %s, ip_addr: %s\n", hostname, ip_addr);
			}
		}
	}
}
