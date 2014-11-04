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
extern int git_https_checking;
extern int git_ssh_checking;
extern char git_conf_filename[PATH_MAX];
extern int is_opened_gitconf;

/* for each SYSCALL_SOCKET syscall, create one pfs_socket_info struct to preserve the socket info. */
void socket_process(int fd, int domain, int type, int protocol) {
	if(domain == AF_INET || domain == AF_INET6) {
		char domain_type[10];
		if(domain == AF_INET)
			strcpy(domain_type, "AF_INET");
		else
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
		/* if the socket fd has been occupied, first remove the existing socket item from netlist_table, and then insert the new pfs_socket_info into the table. */
		if(!existed_socket) {
			hash_table_insert(netlist_table, buf, p_sock);
		} else {
			free(existed_socket);
			hash_table_remove(netlist_table, buf);
			hash_table_insert(netlist_table, buf, p_sock);
		}
	}
}

/* fill in the pfs_socket_info struct according to the connect syscall: ip_addr, port, host_name, service_name, type_name */
void connect_process(int fd, struct sockaddr *sock_addr) {
	struct pfs_socket_info *existed_socket;
	char buf[10];
	snprintf(buf, sizeof(buf), "%d", fd);
	existed_socket = (struct pfs_socket_info *)hash_table_lookup(netlist_table, buf);
	if(existed_socket) {
		int s;
		char host_buf[100], service_name_buf[100], ip_addr[1024];
		/* obtain hostname and port info from socket_addr
		 * the hostname obtained from getnameinfo may not be the hostname used by the application, 
		 * so ip_table will be checked to get the hostname used by the application
		 */
		s = getnameinfo(sock_addr, sizeof(*sock_addr), host_buf, sizeof(host_buf), service_name_buf, sizeof(service_name_buf), 100| 100);
		if (s == 0) {
			/* obtain ip_addr info from socket_addr */
			inet_ntop(sock_addr->sa_family, get_in_addr(sock_addr), ip_addr, sizeof(ip_addr));
			strcpy(existed_socket->ip_addr, ip_addr);

			/* the priority of hostname information for an ip_addr: ip_table > host_buf */
			char *item_value;
			item_value = (char *)hash_table_lookup(ip_table, ip_addr);
			if(item_value)
				strcpy(existed_socket->host_name, item_value);
			else {
				strcpy(existed_socket->host_name, host_buf);
			}
			existed_socket->port = get_in_port(sock_addr);
			strcpy(existed_socket->service_name, service_name_buf);
			if(strcmp(host_buf, "github.com") == 0) {
				if(strcmp(service_name_buf, "https") == 0) {
					git_https_checking = 1;
				}
				if(strcmp(service_name_buf, "ssh") == 0) {
					git_ssh_checking = 1;
				}
				is_opened_gitconf = 0;
			}
			char type_name[20];
			get_socket_type(existed_socket->type, type_name);
			strcpy(existed_socket->type_name, type_name);
			fprintf(netlist_file, "\nid: %d; domain: %d; domain_type: %s; ", existed_socket->id, existed_socket->domain, existed_socket->domain_type);
			fprintf(netlist_file, "type: %d; type_name: %s; ", existed_socket->type, existed_socket->type_name);
			fprintf(netlist_file, "ip_addr: %s; port: %d; host_name: %s; service_name: %s\n\n", existed_socket->ip_addr, existed_socket->port, existed_socket->host_name, existed_socket->service_name);
		}
	}
}

void connect_process32(int fd, struct pfs_kernel_sockaddr_un addr) {
	struct sockaddr *sock_addr;
	sock_addr = (struct sockaddr *) (&addr);
	connect_process(fd, sock_addr);
}

void connect_process64(int fd, struct sockaddr_un addr) {
	struct sockaddr *sock_addr;
	sock_addr = (struct sockaddr *) (&addr);
	connect_process(fd, sock_addr);
}

/* parse the socket data traffic, currently only dns and http service data is parsed. */
void socket_data_parser(int fd, char *data, int length) {
	struct pfs_socket_info *existed_socket;
	char buf[10];
	snprintf(buf, sizeof(buf), "%d", fd);
	existed_socket = (struct pfs_socket_info *) hash_table_lookup(netlist_table, buf);
	if(existed_socket) {
		/* parser dns service data */
		if(existed_socket->port == 53) {
			char hostname[HOSTNAME_MAX], ipaddr[IP_LEN];
			dns_packet_parser((unsigned char *)data, length, hostname, ipaddr);
			if(hostname[0] != '\0' && ipaddr[0] != '\0') {
				/* check whether <hostname, ipaddr> exists in ip_table. if not, insert it. */
				char *existed_hostname;
				existed_hostname = (char *)hash_table_lookup(ip_table, (char *)hostname);
				if(!existed_hostname) {
					char *ip_value;
					ip_value = (char *)malloc(HOSTNAME_MAX);
					strcpy(ip_value, hostname);
					hash_table_insert(ip_table, (char *)ipaddr, (char *)ip_value);
					fprintf(netlist_file, "DNS (Address Record) hostname: %s, ipaddr: %s\n", hostname, ipaddr);
				}
			}
		}
		/* parse http service data */
		if(existed_socket->port == 80) {
			/* obtain http request */
			if(existed_socket->http_checking == 0 && HttpCheck(data, length, 0) == 1) {
				existed_socket->http_checking = 1;
				return;
			}
			/* obtain http response */
			if(existed_socket->http_checking == 1 && HttpCheck(data, length, 1) == 2) {
				existed_socket->http_checking = 2;
				return;
			}
		}
	}
}

/* when a socket is going to be closed, the pfs_socket_info will be checked to see whether the socket is used for git.
 * if yes, the git configuration file will be obtained.
 */
void get_git_conf(int fd, char *executable_name) {
	struct pfs_socket_info *existed_socket;
	char buf[10];
	snprintf(buf, sizeof(buf), "%d",fd);
	existed_socket = (struct pfs_socket_info *) hash_table_lookup(netlist_table, buf);
	if(existed_socket && (strcmp(existed_socket->host_name, "github.com") == 0)) {
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

/* get the ip addr from sockaddr */
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
       return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/* get the port number from sockaddr */
int get_in_port(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return ntohs(((struct sockaddr_in*)sa)->sin_port);
	}
		return ntohs(((struct sockaddr_in6*)sa)->sin6_port);
}

/* obtain http requests and responses 
 * stage = 0: http request; stage = 1: http response
 */
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
