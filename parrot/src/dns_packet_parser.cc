#include "dns_packet_parser.h"

/* analyze the name part and return the length of the name part 
 * the name can be a sequence of labels ending with a zero byte, or a pointer, or a sequence of labels ending with a pointer
 */
int name_len_resolver(unsigned char *data, unsigned char *name) {
	int len, sub_len;
	len = 0;
	/* the first two bits are ones, this is a pointer, so the length of name is 2 bytes */
	if(((name[0] >> 6) & 0x03) == 0x03) {
		return 2;
	}
	sub_len = (unsigned int)name[len];
	while(name[len] != 0) {
		if(((name[len] >> 6) & 0x03) == 0x03) {
			return len + 2;
		}
		len += (sub_len + 1); //there is one zero bytes between two labels, so add a special 1 to len
		sub_len = (unsigned int)name[len];
	}
	len++; //add the length of the final zero byte into len
	return len;
}

/* analyze the name part and return the hostname 
 * the name can be a sequence of labels ending with a zero byte, or a pointer, or a sequence of labels ending with a pointer
 * current_len: track the length of hostname
 */
void name_resolver(unsigned char *data, unsigned char *name, char hostname[HOSTNAME_MAX], int current_len) {
	if(((name[0] >> 6) & 0x03) == 0x03) { /*the name is a pointer*/
		int position;
		unsigned short offset = ntohs((unsigned short)((name[0] & 0x3f) * 256)) + ntohs((unsigned short)name[1]);
		position = ntohs(offset) & 0x3fff;
		name_resolver(data, data + position, hostname, current_len);
		return;
	} else {
		int len, sub_len;
		len = 0;
		sub_len = (unsigned int)name[len];
		while(name[len] != 0) {
			if(((name[len] >> 6) & 0x03) == 0x03) {
				int position;
				unsigned short offset = ntohs((unsigned short)((name[len] & 0x3f) * 256)) + ntohs((unsigned short)name[len+1]);
				position = ntohs(offset) & 0x3fff;
				name_resolver(data, data + position, hostname, current_len);
				return;
			} else {
				strncat(hostname, (char *)name + len + 1, sub_len);
				strncat(hostname, ".", 1);
				len += (sub_len + 1);
				current_len += (sub_len + 1);
				sub_len = (unsigned int)name[len];
			}
		}
		hostname[--current_len] = '\0';

		/* check whether the hostname is legal. If the hostname contains null character, clear the hostname */
		int i;
		for(i = 0; i < current_len; i++) {
			if(hostname[i] == 0) {
				hostname[0] = '\0';
			}
		}
	}
}

/* parse dns packet, figure out the hostname and ipaddr mapping. */
void dns_packet_parser(unsigned char *data, int size, char hostname[HOSTNAME_MAX], char ip_addr[IP_LEN]) {
	hostname[0] = '\0';
	ip_addr[0] = '\0';

	struct dns_header *dns_h = (struct dns_header *)data;

	/* current_pos records the current location being parsed whithin the DNS packet */
	int current_pos = 0; 
	unsigned short flags;
	flags = ntohs(dns_h->flags); //dns_h->flags is the 3rd and 4th bytes of a DNS header
	int qr, opcode;
	qr = (flags >> 15) & 0x0001; //qr = 0: a query packet; qr = 1: a response packet.
	opcode = (flags >> 11) & 0x000f;

	//only deal with standard DNS answer packets (Opcode = 0) with only one question (qdcount = 1)
	if(qr == 1 && opcode == 0 && ntohs(dns_h->qdcount) == 1) {
		//parse dns_qeustion
		unsigned char *qname = data + 12; /* the length of a DNS header is 12, qname points to the starting location of a DNS question */
		current_pos += 12;

		int qname_len;
		qname_len = name_len_resolver(data, qname);
		//struct dns_question *dns_q = (struct dns_question *) (qname + qname_len);
		current_pos += qname_len; //current_pos points to dns_q->qtype

		//parse dns_answer
		unsigned char *name = qname + qname_len + 4; //name is the beginning of a DNS answer, the length of the dns_question structure is 4 bytes
		current_pos += 4; //the length of the dns_question structure is 4 bytes
		struct dns_answer *dns_a;
		int name_len;
		name_len = name_len_resolver(data, name);
		char tmpname[HOSTNAME_MAX];
		name_resolver(data, name, hostname, 0);
		if(hostname[0] == '\0')
			return;
		dns_a = (struct dns_answer *)(name + name_len);
		current_pos += name_len;
		int i;
		/* type = 1: A IPv4 address record; type = 5: CNAME (Canonical name record); type = 28: A IPv6 address record
		 * answer_class = 1: Internet Addresses; rdlength is the length of rdata
		 */
		if((ntohs(dns_a->type) == 1 || ntohs(dns_a->type) == 28 || ntohs(dns_a->type) == 5) && ntohs(dns_a->answer_class) == 1 && (ntohs(dns_a->rdlength)) > 0) {
			unsigned char *rdata;
			rdata = (unsigned char *) (name + name_len + 10); //the length of dns_answer struct is 10
			current_pos += 10;

			if(ntohs(dns_a->type) == 1) {
				char buf[4];
				strcpy(ip_addr, "");
				for(i = 0 ; i < (ntohs(dns_a->rdlength)); i++) {
					snprintf(buf, sizeof(buf), "%u", rdata[i]);
					strcat(ip_addr, buf);
					if(i < (ntohs(dns_a->rdlength) - 1))
						strcat(ip_addr, ".");
				}
			}

			if(ntohs(dns_a->type) == 28) {
				char buf[4];
				strcpy(ip_addr, "");
				for(i = 0 ; i < (ntohs(dns_a->rdlength)); i++) {
					snprintf(buf, sizeof(buf), "%02X", (unsigned int)rdata[i]);
					strcat(ip_addr, buf);
					if((i < (ntohs(dns_a->rdlength) - 2)) && i%2 == 1)
						strcat(ip_addr, ".");
				}
			}

			if(ntohs(dns_a->type) == 5) {
				int first_answer_len;
				first_answer_len = name_len_resolver(data, (unsigned char *)(data + current_pos));
				strcpy(tmpname, "");
				name_resolver(data, (unsigned char *)(data + current_pos), tmpname, 0);
				if(tmpname[0] == '\0')
					return;
				current_pos += first_answer_len; 
				
				int answer_no = 1;
				while(answer_no < ntohs(dns_h->ancount) - 1) {
					char next_hostname[HOSTNAME_MAX];
					unsigned char *next_answer_name = (unsigned char *)(data + current_pos); //current_pos is the location of the second answer
					int next_answer_name_len = name_len_resolver(data, next_answer_name);
					struct dns_answer *next_dns_answer = (struct dns_answer *)(next_answer_name + next_answer_name_len);
					strcpy(next_hostname, "");
					name_resolver(data, next_answer_name, next_hostname, 0);
					if(next_hostname[0] == '\0')
						return;
					if((ntohs(next_dns_answer->type) == 1 || ntohs(next_dns_answer->type) == 28) && ntohs(next_dns_answer->answer_class) == 1) {
						unsigned char *next_dns_answer_rdata = (unsigned char *)(next_answer_name + next_answer_name_len + 10);
						strcpy(tmpname, "");
						name_resolver(data, next_dns_answer_rdata, tmpname, 0);
						if(tmpname[0] == '\0')
							return;
						current_pos += next_answer_name_len + 10 + ntohs(next_dns_answer->rdlength);
					} else {
						return;
					}
					
				}

				//the last answer is <the last alias, ip address>
				unsigned char *last_answer_name = (unsigned char *)(data + current_pos); //current_pos is the location of the last answer
				int last_answer_name_len = name_len_resolver(data, last_answer_name);
				struct dns_answer *dns_last_answer = (struct dns_answer *)(last_answer_name + last_answer_name_len);
				char last_hostname[HOSTNAME_MAX];
				strcpy(last_hostname, "");
				name_resolver(data, last_answer_name, last_hostname, 0);
				if(last_hostname[0] == '\0')
					return;
				if((ntohs(dns_last_answer->type) == 1 || ntohs(dns_last_answer->type) == 28) && ntohs(dns_last_answer->answer_class) == 1) {
					unsigned char *dns_last_answer_rdata = (unsigned char *)(last_answer_name + last_answer_name_len + 10);
					int k;
					char buf[4];
					strcpy(ip_addr, "");
					for(k=0; k<ntohs(dns_last_answer->rdlength); k++) {
						snprintf(buf, sizeof(buf), "%u", dns_last_answer_rdata[k]);
						strcat(ip_addr, buf);
						if(k < (ntohs(dns_last_answer->rdlength) - 1))
							strcat(ip_addr, ".");
					}
				}
			}
		}
	}
}
