#include<netinet/ip_icmp.h>   //Provides declarations for icmp header
#include<netinet/udp.h>   //Provides declarations for udp header
#include<netinet/tcp.h>   //Provides declarations for tcp header
#include<netinet/ip.h>    //Provides declarations for ip header
#include<sys/socket.h>
#include<arpa/inet.h>

void *get_in_addr(struct sockaddr *sa);
int get_in_port(struct sockaddr *sa);
int HttpCheck(char *buffer, int size);
void ProcessPacket(unsigned char* , int);
void print_ip_header(unsigned char* , int);
void print_ip_packet(unsigned char* , int);
void print_tcp_packet(unsigned char* , int);
void print_udp_packet(unsigned char * , int);
void print_icmp_packet(unsigned char* , int);
void PrintData (unsigned char* , int);
