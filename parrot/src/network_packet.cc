#include "pfs_sys.h"
#include "pfs_types.h"
#include "pfs_sysdeps.h"
#include "pfs_table.h"
#include "pfs_process.h"
#include "pfs_service.h"
#include "network_packet.h"

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

int HttpCheck(char *buffer, int size) {
	if(buffer[0] == 'G' && buffer[1] == 'E' && buffer[2] == 'T') {
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
	if(buffer[0] == 'H' && buffer[1] == 'T' && buffer[2] == 'T' && buffer[3] == 'P' && buffer[4] == '/') {
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
