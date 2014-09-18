#include "pfs_sys.h"
#include "pfs_types.h"
#include "pfs_sysdeps.h"
#include "pfs_table.h"
#include "pfs_process.h"
#include "pfs_service.h"
#include "network_packet.h"
#include "dns_packet_parser.h"

extern "C" {
#include "debug.h"
#include "full_io.h"
#include "file_cache.h"
#include "stringtools.h"
#include "hash_table.h"
}

#include "pfs_types.h"
#include "pfs_search.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/file.h>
#include <sys/poll.h>
#include <utime.h>

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/un.h>
#include <time.h>
#include <arpa/inet.h>
#include <netdb.h>

extern FILE *netlist_file;
extern struct hash_table *netlist_table;
extern struct hash_table *ip_table;
extern struct hash_table *dns_alias_table;
extern int git_https_checking;
extern int git_ssh_checking;
extern char git_conf_filename[PATH_MAX];
extern int is_opened_gitconf;

void socket_process(int fd, int domain, int type, int protocol) {
	if(netlist_table) {
		if(domain == AF_INET || domain == AF_INET6) {
			char domain_type[10];
			if(domain == AF_INET)
				strcpy(domain_type, "AF_INET");
			if(domain == AF_INET6)
				strcpy(domain_type, "AF_INET6");
			struct pfs_socket_info *p_sock, *existed_socket;
			p_sock = (struct pfs_socket_info *) malloc(sizeof(struct pfs_socket_info));
			char buf[10];
			snprintf(buf, sizeof(buf), "%d", fd);
			p_sock->id = fd;
			p_sock->domain = domain;
			strcpy(p_sock->domain_type, domain_type);
			p_sock->type = type;
			p_sock->protocol = protocol;
			strcpy(p_sock->ip_addr, "");
			p_sock->port = -1;
			strcpy(p_sock->host_name, "");
			strcpy(p_sock->service_name, "");
			strcpy(p_sock->resource_path, "");
			p_sock->resource_status = -1;
			p_sock->http_checking = 0;

			existed_socket = (struct pfs_socket_info *)hash_table_lookup(netlist_table, buf);
			if(!existed_socket) {
				hash_table_insert(netlist_table, buf, p_sock);
//				fprintf(netlist_file, "create one new socket\n");
//				fprintf(netlist_file, "\nid: %d; domain: %d; domain_type: %s; ", p_sock->id, p_sock->domain, p_sock->domain_type);
//				fprintf(netlist_file, "ip_addr: %s; port: %d; host_name: %s; service_name: %s; resource_path: %s; resource_status: %d\n\n", p_sock->ip_addr, p_sock->port, p_sock->host_name, p_sock->service_name, p_sock->resource_path, p_sock->resource_status);
			} else {
				free(existed_socket);
				hash_table_remove(netlist_table, buf);
				hash_table_insert(netlist_table, buf, p_sock);
//				fprintf(netlist_file, "create one new socket, but this socket has ever been used, first delete the old one, and here is the new one\n");
//				fprintf(netlist_file, "\nid: %d; domain: %d; domain_type: %s; ", p_sock->id, p_sock->domain, p_sock->domain_type);
//				fprintf(netlist_file, "ip_addr: %s; port: %d; host_name: %s; service_name: %s; resource_path: %s; resource_status: %d\n\n", p_sock->ip_addr, p_sock->port, p_sock->host_name, p_sock->service_name, p_sock->resource_path, p_sock->resource_status);
			}
		}
	}
}

void connect_process(int fd, struct sockaddr_un addr) {
	struct pfs_socket_info *existed_socket;
	char buf[10];
	snprintf(buf, sizeof(buf), "%d", fd);
	existed_socket = (struct pfs_socket_info *)hash_table_lookup(netlist_table, buf);
	if(existed_socket) {
		int s;
		char host_buf[100], port_buf[100], ip_addr[1024];
		struct sockaddr *sock_addr;
		sock_addr = (struct sockaddr *) (&addr);
		s = getnameinfo(sock_addr, sizeof(*sock_addr), host_buf, sizeof(host_buf), port_buf, sizeof(port_buf), 100| 100);
		if (s == 0) {
			inet_ntop(sock_addr->sa_family, get_in_addr(sock_addr), ip_addr, sizeof(ip_addr));
			strcpy(existed_socket->ip_addr, ip_addr);
			char *item_value;
			item_value = (char *)hash_table_lookup(ip_table, ip_addr);
			if(item_value)
				strcpy(existed_socket->host_name, item_value);
			else {
				item_value = (char *)hash_table_lookup(dns_alias_table, host_buf);
				if(item_value)
					strcpy(existed_socket->host_name, item_value);
				else
					strcpy(existed_socket->host_name, host_buf);
			}
			existed_socket->port = get_in_port(sock_addr);
			strcpy(existed_socket->service_name, port_buf);
			if(strcmp(host_buf, "github.com") == 0) {
				if(strcmp(port_buf, "https") == 0) {
					git_https_checking = 1;
				}
				if(strcmp(port_buf, "ssh") == 0) {
					git_ssh_checking = 1;
				}
			}
			char type_name[20];
			get_socket_type(existed_socket->type, type_name);
			strcpy(existed_socket->type_name, type_name);
			fprintf(netlist_file, "\nid: %d; domain: %d; domain_type: %s; ", existed_socket->id, existed_socket->domain, existed_socket->domain_type);
			fprintf(netlist_file, "type: %d; type_name: %s; ", existed_socket->type, existed_socket->type_name);
			fprintf(netlist_file, "ip_addr: %s; port: %d; host_name: %s; service_name: %s\n\n", existed_socket->ip_addr, existed_socket->port, existed_socket->host_name, existed_socket->service_name);
		}
		is_opened_gitconf = 0;
	}
}

void socket_data_parser(int fd, char *data, int length) {
	if(netlist_table) {
		struct pfs_socket_info *existed_socket;
		char buf[10];
		snprintf(buf, sizeof(buf), "%d", fd);
		existed_socket = (struct pfs_socket_info *) hash_table_lookup(netlist_table, buf);
		if(existed_socket) {
			if(existed_socket->port == 53) {
				char hostname[HOSTNAME_MAX], ipaddr[IP_LEN], cname_alias[HOSTNAME_MAX];
				dns_packet_parser((unsigned char *)data, length, hostname, ipaddr, cname_alias);
				if(ipaddr[0] != '\0') {
					char *existed_hostname;
					existed_hostname = (char *)hash_table_lookup(ip_table, (char *)hostname);
					if(!existed_hostname) {
						char *ip_value;
						ip_value = (char *)malloc(HOSTNAME_MAX);
						strcpy(ip_value, hostname);
						hash_table_insert(ip_table, (char *)ipaddr, (char *)ip_value);
						fprintf(netlist_file, "DNS (Address Record) hostname: %s, ipaddr: %s\n", hostname, ipaddr);
					}
				} else if(cname_alias[0] != '\0') {
					char *existed_cname_alias;
					existed_cname_alias = (char *)hash_table_lookup(dns_alias_table, (char *)cname_alias);
					if(!existed_cname_alias) {
						char *dns_alias_table_value;
						dns_alias_table_value = (char *)malloc(HOSTNAME_MAX);
						strcpy(dns_alias_table_value, hostname);
//						fprintf(netlist_file, "One alias of `%s' is `%s'\n", hostname, cname_alias);
						hash_table_insert(dns_alias_table, (char *)cname_alias, (char *)dns_alias_table_value);
						fprintf(netlist_file, "DNS (CNAME) hostname: %s, cname: %s\n", hostname, cname_alias);
					}
				}
			} else {
				if(existed_socket->http_checking == 0 && HttpCheck(data, length, 0) == 1) {
					existed_socket->http_checking = 1;
					return;
				}
				if(existed_socket->http_checking == 1 && HttpCheck(data, length, 1) == 2) {
					existed_socket->http_checking = 2;
					return;
				}
			}
		}
	}
}

void get_git_conf(int fd, char *executable_name) {
	if(netlist_table) {
		struct pfs_socket_info *existed_socket;
		char buf[10];
		snprintf(buf, sizeof(buf), "%d",fd);
		existed_socket = (struct pfs_socket_info *) hash_table_lookup(netlist_table, buf);
		if(existed_socket) {
//		fprintf(netlist_file, "closing socket %d \n", existed_socket->id);
			if(strcmp(existed_socket->host_name, "github.com") == 0) {
				char *s;
				if(strcmp(existed_socket->service_name, "https") == 0 && git_https_checking == 1) {
					git_https_checking = 0;
					s = strstr(executable_name, "git");
					if(s != NULL && s[3] == '\0') {
						fprintf(netlist_file, "this is git https\n");
						FILE *git_conf_file;
						if(is_opened_gitconf == 0) {
							is_opened_gitconf = 1;
							git_conf_file = fopen(git_conf_filename, "r");
							char line[PATH_MAX];
							fprintf(netlist_file, "git config file: \n");
							while(fgets(line, PATH_MAX, git_conf_file) != NULL) {
								fprintf(netlist_file, "%s", line);
							}
							fprintf(netlist_file, "\n");
						}
					}
				}
				if(strcmp(existed_socket->service_name, "ssh") == 0 && git_ssh_checking == 1) {
					git_ssh_checking = 0;
					s = strstr(executable_name, "ssh");
					if(s != NULL && s[3] == '\0') {
						fprintf(netlist_file, "this is git ssh\n");
						FILE *git_conf_file;
						if(is_opened_gitconf == 0) {
							is_opened_gitconf = 1;
							git_conf_file = fopen(git_conf_filename, "r");
							char line[PATH_MAX];
							fprintf(netlist_file, "git config file: \n");
							while(fgets(line, PATH_MAX, git_conf_file) != NULL) {
								fprintf(netlist_file, "%s", line);
							}
							fprintf(netlist_file, "\n");
						}
					}
				}
			}
		}
	}
}
void get_socket_family(int family, char family_name[20]) {
	switch(family) {
		case AF_UNIX:
			strcpy(family_name, "unix");
			break;
		case AF_INET:
			strcpy(family_name, "inet");
			break;
		case AF_INET6:
			strcpy(family_name, "inet6");
			break;
		case AF_UNSPEC:
			strcpy(family_name, "unspecified");
			break;
		default:
			strcpy(family_name, "unknown");
	}
}

void get_socket_type(int type, char type_name[20]) {
	switch(type) {
		case SOCK_STREAM:
			strcpy(type_name, "stream");
			break;
		case SOCK_DGRAM:
			strcpy(type_name, "datagram");
			break;
		case SOCK_SEQPACKET:
			strcpy(type_name, "seqpacket");
			break;
		case SOCK_RAW:
			strcpy(type_name, "raw");
			break;
		default:
			strcpy(type_name, "unknown");
	}
}

void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
       return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int get_in_port(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return ntohs(((struct sockaddr_in*)sa)->sin_port);
	}
		return ntohs(((struct sockaddr_in6*)sa)->sin6_port);
}

int HttpCheck(char *buffer, int size, int stage) {
	if(stage == 0 && buffer[0] == 'G' && buffer[1] == 'E' && buffer[2] == 'T') {
		char *s;
		s = strchr(buffer, ' ');
		s ++;
		s = strchr(s, ' ');
		s ++;
		if(s[0] == 'H' && s[1] == 'T' && s[2] == 'T' && s[3] == 'P') {
			fprintf(netlist_file, "\nHTTP dependency item -- request:\n%.*s\n", size, buffer);
			return 1;
		}
	}
	if(stage == 1 && buffer[0] == 'H' && buffer[1] == 'T' && buffer[2] == 'T' && buffer[3] == 'P' && buffer[4] == '/') {
		if(size > 500)
			fprintf(netlist_file, "\nHTTP dependency item -- response (only first 500 bytes):\n%.*s\n", 500, buffer);
		else
			fprintf(netlist_file, "\nHTTP dependency item -- response:\n%.*s\n", size, buffer);
		return 2;
	}
	return -1;
}

void GetTransportProtocol(unsigned char* buffer, int size, char protocol[20])
{
	//Get the IP Header part of this packet
	struct iphdr *iph = (struct iphdr*)buffer;
	fprintf(netlist_file, "ProcessPacket: protocol: %d\n", iph->protocol);
	switch (iph->protocol) //Check the Protocol and do accordingly...
	{
		case IPPROTO_ICMP:  //ICMP Protocol
			strcpy(protocol, "ICMP");
			break;
		case IPPROTO_TCP:  //TCP Protocol
			strcpy(protocol, "TCP");
			break;
        case IPPROTO_UDP: //UDP Protocol
			strcpy(protocol, "UDP");
			break;
		default: //Some Other Protocol like ARP etc.
			break;
	}
}

void ProcessPacket(unsigned char* buffer, int size)
{
	//Get the IP Header part of this packet
	struct iphdr *iph = (struct iphdr*)buffer;
	fprintf(netlist_file, "ProcessPacket: protocol: %d\n", iph->protocol);
	switch (iph->protocol) //Check the Protocol and do accordingly...
	{
		case IPPROTO_ICMP:  //ICMP Protocol
			print_icmp_packet(buffer,size);
			break;
		case IPPROTO_IP:
			print_ip_packet(buffer , size);
			break;
		case IPPROTO_TCP:  //TCP Protocol
			print_tcp_packet(buffer , size);
			break;
        case IPPROTO_UDP: //UDP Protocol
			print_udp_packet(buffer , size);
			break;
		default: //Some Other Protocol like ARP etc.
			break;
	}
}

void print_ip_header(unsigned char* Buffer, int Size)
{
	unsigned short iphdrlen;
	struct sockaddr_in source,dest;
	struct iphdr *iph = (struct iphdr *)Buffer;
	iphdrlen =iph->ihl*4;

	memset(&source, 0, sizeof(source));
	source.sin_addr.s_addr = iph->saddr;

	memset(&dest, 0, sizeof(dest));
	dest.sin_addr.s_addr = iph->daddr;

	fprintf(netlist_file,"\n");
	fprintf(netlist_file,"IP Header\n");
	fprintf(netlist_file,"   |-IP Version        : %d\n",(unsigned int)iph->version);
	fprintf(netlist_file,"   |-IP Header Length  : %d DWORDS or %d Bytes\n",(unsigned int)iph->ihl,((unsigned int)(iph->ihl))*4);
	fprintf(netlist_file,"   |-Type Of Service   : %d\n",(unsigned int)iph->tos);
	fprintf(netlist_file,"   |-IP Total Length   : %d  Bytes(Size of Packet)\n",ntohs(iph->tot_len));
	fprintf(netlist_file,"   |-Identification    : %d\n",ntohs(iph->id));
	//fprintf(netlist_file,"   |-Reserved ZERO Field   : %d\n",(unsigned int)iphdr->ip_reserved_zero);
	//fprintf(netlist_file,"   |-Dont Fragment Field   : %d\n",(unsigned int)iphdr->ip_dont_fragment);
	//fprintf(netlist_file,"   |-More Fragment Field   : %d\n",(unsigned int)iphdr->ip_more_fragment);
	fprintf(netlist_file,"   |-TTL      : %d\n",(unsigned int)iph->ttl);
	fprintf(netlist_file,"   |-Protocol : %d\n",(unsigned int)iph->protocol);
	fprintf(netlist_file,"   |-Checksum : %d\n",ntohs(iph->check));
	fprintf(netlist_file,"   |-Source IP        : %s\n",inet_ntoa(source.sin_addr));
	fprintf(netlist_file,"   |-Destination IP   : %s\n",inet_ntoa(dest.sin_addr));
}

void print_ip_packet(unsigned char* Buffer, int Size)
{
	unsigned short iphdrlen;

	struct iphdr *iph = (struct iphdr *)Buffer;
	iphdrlen = (iph->ihl)*4;

	fprintf(netlist_file,"\n\n***********************IP Packet*************************\n");
	print_ip_header(Buffer,Size);

	fprintf(netlist_file,"IP Header\n");
	PrintData(Buffer,iphdrlen);

	fprintf(netlist_file,"Data Payload\n");
	PrintData(Buffer + iphdrlen, (Size - iph->ihl*4) );

	fprintf(netlist_file,"\n###########################################################");
}

void print_tcp_packet(unsigned char* Buffer, int Size)
{
	unsigned short iphdrlen;

	struct iphdr *iph = (struct iphdr *)Buffer;
	iphdrlen = (iph->ihl)*4;

	struct tcphdr *tcph=(struct tcphdr*)(Buffer + iphdrlen);

	fprintf(netlist_file,"\n\n***********************TCP Packet*************************\n");

	print_ip_header(Buffer,Size);

	fprintf(netlist_file,"\n");
	fprintf(netlist_file,"TCP Header\n");
	fprintf(netlist_file,"   |-Source Port      : %u\n",ntohs(tcph->source));
	fprintf(netlist_file,"   |-Destination Port : %u\n",ntohs(tcph->dest));
	fprintf(netlist_file,"   |-Sequence Number    : %u\n",ntohl(tcph->seq));
	fprintf(netlist_file,"   |-Acknowledge Number : %u\n",ntohl(tcph->ack_seq));
	fprintf(netlist_file,"   |-Header Length      : %d DWORDS or %d BYTES\n" ,(unsigned int)tcph->doff,(unsigned int)tcph->doff*4);
	//fprintf(netlist_file,"   |-CWR Flag : %d\n",(unsigned int)tcph->cwr);
	//fprintf(netlist_file,"   |-ECN Flag : %d\n",(unsigned int)tcph->ece);
	fprintf(netlist_file,"   |-Urgent Flag          : %d\n",(unsigned int)tcph->urg);
	fprintf(netlist_file,"   |-Acknowledgement Flag : %d\n",(unsigned int)tcph->ack);
	fprintf(netlist_file,"   |-Push Flag            : %d\n",(unsigned int)tcph->psh);
	fprintf(netlist_file,"   |-Reset Flag           : %d\n",(unsigned int)tcph->rst);
	fprintf(netlist_file,"   |-Synchronise Flag     : %d\n",(unsigned int)tcph->syn);
	fprintf(netlist_file,"   |-Finish Flag          : %d\n",(unsigned int)tcph->fin);
	fprintf(netlist_file,"   |-Window         : %d\n",ntohs(tcph->window));
	fprintf(netlist_file,"   |-Checksum       : %d\n",ntohs(tcph->check));
	fprintf(netlist_file,"   |-Urgent Pointer : %d\n",tcph->urg_ptr);
	fprintf(netlist_file,"\n");
	fprintf(netlist_file,"                        DATA Dump                         ");
	fprintf(netlist_file,"\n");

	fprintf(netlist_file,"IP Header\n");
	PrintData(Buffer,iphdrlen);

	fprintf(netlist_file,"TCP Header\n");
	PrintData(Buffer+iphdrlen,tcph->doff*4);

	fprintf(netlist_file,"Data Payload\n");
	PrintData(Buffer + iphdrlen + tcph->doff*4 , (Size - tcph->doff*4-iph->ihl*4) );

	fprintf(netlist_file,"\n###########################################################");
}

void print_udp_packet(unsigned char *Buffer , int Size)
{
	unsigned short iphdrlen;
	struct iphdr *iph = (struct iphdr *)Buffer;
	iphdrlen = iph->ihl*4;
	struct udphdr *udph = (struct udphdr*)(Buffer + iphdrlen);
	fprintf(netlist_file,"\n\n***********************UDP Packet*************************\n");
	print_ip_header(Buffer,Size);
	fprintf(netlist_file,"\nUDP Header\n");
	fprintf(netlist_file,"   |-Source Port      : %d\n" , ntohs(udph->source));
	fprintf(netlist_file,"   |-Destination Port : %d\n" , ntohs(udph->dest));
	fprintf(netlist_file,"   |-UDP Length       : %d\n" , ntohs(udph->len));
	fprintf(netlist_file,"   |-UDP Checksum     : %d\n" , ntohs(udph->check));

	fprintf(netlist_file,"\n");
	fprintf(netlist_file,"IP Header\n");
	PrintData(Buffer , iphdrlen);

	fprintf(netlist_file,"UDP Header\n");
	PrintData(Buffer+iphdrlen , sizeof udph);

	fprintf(netlist_file,"Data Payload\n");
	PrintData(Buffer + iphdrlen + sizeof udph ,( Size - sizeof udph - iph->ihl * 4 ));

	fprintf(netlist_file,"\n###########################################################");
}

void print_icmp_packet(unsigned char* Buffer , int Size)
{
	unsigned short iphdrlen;
	struct iphdr *iph = (struct iphdr *)Buffer;
	iphdrlen = iph->ihl*4;
	struct icmphdr *icmph = (struct icmphdr *)(Buffer + iphdrlen);

	fprintf(netlist_file,"\n\n***********************ICMP Packet*************************\n");
	print_ip_header(Buffer , Size);

	fprintf(netlist_file,"\n");

	fprintf(netlist_file,"ICMP Header\n");
	fprintf(netlist_file,"   |-Type : %d",(unsigned int)(icmph->type));

	if((unsigned int)(icmph->type) == 11)
		fprintf(netlist_file,"  (TTL Expired)\n");
	else if((unsigned int)(icmph->type) == ICMP_ECHOREPLY)
		fprintf(netlist_file,"  (ICMP Echo Reply)\n");
	fprintf(netlist_file,"   |-Code : %d\n",(unsigned int)(icmph->code));
	fprintf(netlist_file,"   |-Checksum : %d\n",ntohs(icmph->checksum));
	//fprintf(netlist_file,"   |-ID       : %d\n",ntohs(icmph->id));
	//fprintf(netlist_file,"   |-Sequence : %d\n",ntohs(icmph->sequence));
	fprintf(netlist_file,"\n");
	fprintf(netlist_file,"IP Header\n");
	PrintData(Buffer,iphdrlen);
	fprintf(netlist_file,"UDP Header\n");
	PrintData(Buffer + iphdrlen , sizeof icmph);

	fprintf(netlist_file,"Data Payload\n");
	PrintData(Buffer + iphdrlen + sizeof icmph , (Size - sizeof icmph - iph->ihl * 4));
	fprintf(netlist_file,"\n###########################################################");
}

void PrintData (unsigned char* data , int Size)
{
	int i, j;
	for(i=0 ; i < Size ; i++) {
		if( i!=0 && i%16==0)   //if one line of hex printing is complete...
		{
			fprintf(netlist_file,"         ");
			for(j=i-16 ; j<i ; j++)
			{
				//if(data[j]>=32 && data[j]<=128)
				fprintf(netlist_file,"%c",(unsigned char)data[j]); //if its a number or alphabet 
				//else fprintf(netlist_file,"."); //otherwise print a dot
			}
			fprintf(netlist_file,"\n");
		}
		if(i%16==0) fprintf(netlist_file,"   ");
			fprintf(netlist_file," %02X",(unsigned int)data[i]);

		if( i==Size-1)  //print the last spaces
		{
			for(j=0;j<15-i%16;j++) fprintf(netlist_file,"   "); //extra spaces
			fprintf(netlist_file,"         ");
			for(j=i-i%16 ; j<=i ; j++)
			{
				if(data[j]>=32 && data[j]<=128)
					fprintf(netlist_file,"%c",(unsigned char)data[j]);
				//else fprintf(netlist_file,".");
			}
			fprintf(netlist_file,"\n");
		}
	}
}
