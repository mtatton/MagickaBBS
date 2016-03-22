#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <signal.h>
#include "inih/ini.h"
#include "bbs.h"

int mynode;
struct bbs_config conf;

struct user_record *gUser;
int gSocket;

int usertimeout;

void timer_handler(int signum) {
	if (signum == SIGALRM) {
		if (gUser != NULL) {
			gUser->timeleft--;
			
			if (gUser->timeleft <= 0) {
				s_putstring(gSocket, "\r\n\r\nSorry, you're out of time today..\r\n");
				disconnect(gSocket);
			}
			

		} 
		usertimeout--;
		if (usertimeout <= 0) {
			s_putstring(gSocket, "\r\n\r\nTimeout waiting for input..\r\n");
			disconnect(gSocket);
		}		
	}
}

static int handler(void* user, const char* section, const char* name,
                   const char* value)
{
	struct bbs_config *conf = (struct bbs_config *)user;
	
	if (strcasecmp(section, "main") == 0) {
		if (strcasecmp(name, "bbs name") == 0) {
			conf->bbs_name = strdup(value);
		} else if (strcasecmp(name, "sysop name") == 0) {
			conf->sysop_name = strdup(value);
		} else if (strcasecmp(name, "nodes") == 0) {
			conf->nodes = atoi(value);
		} else if (strcasecmp(name, "new user level") == 0) {
			conf->newuserlvl = atoi(value);
		}
	} else if (strcasecmp(section, "paths") == 0){
		if (strcasecmp(name, "ansi path") == 0) {
			conf->ansi_path = strdup(value);
		} else if (strcasecmp(name, "bbs path") == 0) {
			conf->bbs_path = strdup(value);
		}
	}
	
	return 1;
}

void s_putchar(int socket, char c) {
	write(socket, &c, 1);
}

void s_putstring(int socket, char *c) {
	write(socket, c, strlen(c));
}

void s_displayansi(int socket, char *file) {
	FILE *fptr;
	char c;
	
	char buffer[256];
	
	sprintf(buffer, "%s/%s.ans", conf.ansi_path, file);
	
	fptr = fopen(buffer, "r");
	if (!fptr) {
		return;
	}
	c = fgetc(fptr);
	while (!feof(fptr)) {
		s_putchar(socket, c);
		c = fgetc(fptr);
	}
	fclose(fptr);
}

char s_getchar(int socket) {
	unsigned char c;
	int len;

	len = read(socket, &c, 1);

	if (len == 0) {
		disconnect(socket);
	}
		
	while (c == 255) {
		len = read(socket, &c, 1);
		if (len == 0) {
			disconnect(socket);
		}
		len = read(socket, &c, 1);
		if (len == 0) {
			disconnect(socket);
		}
		len = read(socket, &c, 1);
		if (len == 0) {
			disconnect(socket);
		}		
	}
	usertimeout = 10;
	return (char)c;
}

char s_getc(int socket) {
	char c = s_getchar(socket);

	s_putchar(socket, c);
	return (char)c;
}

void s_readstring(int socket, char *buffer, int max) {
	int i;
	char c;
	
	for (i=0;i<max;i++) {
		c = s_getchar(socket);
		if ((c == '\b' || c == 127) && i > 0) {
			buffer[i-1] = '\0';
			i -= 2;
			s_putstring(socket, "\e[D \e[D");
			continue;
		}
		
		if (c == '\n' || c == '\r') {
			c = s_getchar(socket);
			return;
		}
		s_putchar(socket, c);
		buffer[i] = c;
		buffer[i+1] = '\0';
	}
}

void s_readpass(int socket, char *buffer, int max) {
	int i;
	char c;
	
	for (i=0;i<max;i++) {
		c = s_getchar(socket);

		if ((c == '\b' || c == 127) && i > 0) {
			buffer[i-1] = '\0';
			i-=2;
			s_putstring(socket, "\e[D \e[D");
			continue;
		}

		if (c == '\n' || c == '\r') {
			c = s_getchar(socket);
			return;
		}
		s_putchar(socket, '*');
		buffer[i] = c;
		buffer[i+1] = '\0';
	}
}

void disconnect(int socket) {
	char buffer[256];
	if (gUser != NULL) {
		save_user(gUser);
	}
	sprintf(buffer, "%s/nodeinuse.%d", conf.bbs_path, mynode);
	remove(buffer);
	close(socket);
	exit(0);
}

void runbbs(int socket, char *config_path) {
	char buffer[256];
	char password[17];

	struct stat s;
	FILE *nodefile;
	int i;
	char iac_echo[] = {255, 251, 1, '\0'};
	char iac_sga[] = {255, 251, 3, '\0'};
	struct user_record *user;
	struct tm thetime;
	struct tm oldtime;
	time_t now;
	struct itimerval itime;
	struct sigaction sa;
	
	
	write(socket, iac_echo, 3);
	write(socket, iac_sga, 3);

	

	sprintf(buffer, "Magicka BBS v%d.%d (%s) Loading...\r\n", VERSION_MAJOR, VERSION_MINOR, VERSION_STR);
	s_putstring(socket, buffer);


	// Load BBS data
	if (ini_parse(config_path, handler, &conf) <0) {
		printf("Unable to load configuration ini (%s)!\n", config_path);
		exit(-1);
	}	
	

	
	// find out which node we are
	mynode = 0;
	for (i=1;i<=conf.nodes;i++) {
		sprintf(buffer, "%s/nodeinuse.%d", conf.bbs_path, i);
		if (stat(buffer, &s) != 0) {
			mynode = i;
			nodefile = fopen(buffer, "w");
			if (!nodefile) {
				printf("Error opening nodefile!\n");
				close(socket);
				exit(1);
			}
			
			fputs("UNKNOWN", nodefile);
			fclose(nodefile);
			
			break;
		}
	}
	
	if (mynode == 0) {
		s_putstring(socket, "Sorry, all nodes are in use. Please try later\r\n");
		close(socket);
		exit(1);
	}
	gUser = NULL;
	gSocket = socket;
	usertimeout = 10;
	
	memset (&sa, 0, sizeof (sa));
	sa.sa_handler = &timer_handler;
	sa.sa_flags = SA_RESTART;
	sigaction (SIGALRM, &sa, 0);
	
	itime.it_interval.tv_sec = 60;
	itime.it_interval.tv_usec = 0;
	itime.it_value.tv_sec = 60;
	itime.it_value.tv_usec = 0;
	
	setitimer (ITIMER_REAL, &itime, 0);
	
	s_displayansi(socket, "issue");
	

	
	s_putstring(socket, "Enter your Login Name or NEW to create an account\r\n");
	s_putstring(socket, "Login:> ");
	
	s_readstring(socket, buffer, 25);

	if (strcasecmp(buffer, "new") == 0) {
		user = new_user(socket);
	} else {
		s_putstring(socket, "\r\nPassword:> ");
		s_readpass(socket, password, 16);
		user = check_user_pass(socket, buffer, password);
		if (user == NULL) {
			s_putstring(socket, "\r\nIncorrect Login.\r\n");
			disconnect(socket);
		}
		
		for (i=1;i<=conf.nodes;i++) {
			sprintf(buffer, "%s/nodeinuse.%d", conf.bbs_path, i);
			if (stat(buffer, &s) == 0) {
				nodefile = fopen(buffer, "r");
				if (!nodefile) {
					printf("Error opening nodefile!\n");
					disconnect(socket);
				}
				fgets(buffer, 256, nodefile);
				
				buffer[strlen(buffer) - 1] = '\0';
				
				if (strcasecmp(user->loginname, buffer) == 0) {
					fclose(nodefile);
					s_putstring(socket, "You are already logged in.\r\n");
					disconnect(socket);
				}
				fclose(nodefile);
			}
		}
	}
	
	sprintf(buffer, "%s/nodeinuse.%d", conf.bbs_path, mynode);
	nodefile = fopen(buffer, "w");
	if (!nodefile) {
		printf("Error opening nodefile!\n");
		close(socket);
		exit(1);
	}
			
	fputs(user->loginname, nodefile);
	fclose(nodefile);	
	
	// do post-login
	// check time left
	now = time(NULL);
	localtime_r(&now, &thetime);
	localtime_r(&user->laston, &oldtime);
	
	if (thetime.tm_mday != oldtime.tm_mday || thetime.tm_mon != oldtime.tm_mon || thetime.tm_year != oldtime.tm_year) {
		user->timeleft = user->sec_info->timeperday;
		user->laston = now;
		save_user(user);
	}		
	gUser = user;

	
	// main menu
	main_menu(socket, user);
	disconnect(socket);
}