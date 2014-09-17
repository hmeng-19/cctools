#include <netinet/ip_icmp.h>   //Provides declarations for icmp header
#include <netinet/udp.h>   //Provides declarations for udp header
#include <netinet/tcp.h>   //Provides declarations for tcp header
#include <netinet/ip.h>    //Provides declarations for ip header
#include <sys/socket.h>
#include <arpa/inet.h>

void socket_process(int fd, int domain, int type, int protocol);
void connect_process(int fd, struct sockaddr_un addr);
void socket_data_parser(int fd, char *data, int length);
void get_git_conf(int fd, char *executable_name);
void get_socket_family(int family, char family_name[20]);
void get_socket_type(int type, char type_name[20]);
void *get_in_addr(struct sockaddr *sa);
int get_in_port(struct sockaddr *sa);
int HttpCheck(char *buffer, int size, int stage);
void GetTransportProtocol(unsigned char* buffer, int size, char protocol[20]);
void ProcessPacket(unsigned char* , int);
void print_ip_header(unsigned char* , int);
void print_ip_packet(unsigned char* , int);
void print_tcp_packet(unsigned char* , int);
void print_udp_packet(unsigned char * , int);
void print_icmp_packet(unsigned char* , int);
void PrintData (unsigned char* , int);
