/**
 * Suraj Kurapati <skurapat@ucsc.edu>
 * CMPS-150, Spring04, final project
 *
 * SimpleFTP client.
**/

#include "model.h"
#include "client.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

// globals
	char g_pwd[PATH_MAX+1];

// funcs
int main(int a_argc, char **ap_argv)
{
	// variables
		int socket;
		
	// check args
		if(a_argc < 3)
		{
			printf("Usage: %s server_name port_number\n", ap_argv[0]);
			return 1;
		}
		
	// init vars
		realpath(".", g_pwd);
		
	// establish link
		if(!service_create(&socket, ap_argv[1], ap_argv[2]))
		{
			fprintf(stderr, "%s: Unable to connect to %s:%s.\n", ap_argv[0], ap_argv[1], ap_argv[2]);
			return 2;
		}
		
	// establish session
		if(!session_create(socket))
		{
			close(socket);
			
			fprintf(stderr, "%s: Unable to establish session.\n", ap_argv[0]);
			return 3;
		}
		
		printf("\nSession established successfully.");
	
	// handle user commands
		service_loop(socket);
	
	// destroy session
		if(session_destroy(socket))
			printf("Session terminated successfully.\n");
		
	// destroy link
		close(socket);
		
	return 0;
}

Boolean service_create(int *ap_socket, const String a_serverName, const String a_serverPort)
{
	// variables
		struct sockaddr_in serverAddr;
		struct hostent *p_serverInfo;
		
	// create server address
		
		// determine dotted quad str from DNS query
			if((p_serverInfo = gethostbyname(a_serverName)) == NULL)
			{
				herror("service_create()");
				return false;
			}
			
			#ifndef NODEBUG
				printf("service_create(): serverName='%s', serverAddr='%s'\n", a_serverName, inet_ntoa(*((struct in_addr *)p_serverInfo->h_addr)));
			#endif
			
			serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(*((struct in_addr *)p_serverInfo->h_addr)));
			
		serverAddr.sin_family = AF_INET;
		serverAddr.sin_port = htons(strtol(a_serverPort, (char**)NULL, 10));
		
	// create socket
		if((*ap_socket = socket(serverAddr.sin_family, SOCK_STREAM, 0)) < 0)
		{
			perror("service_create()");
			return false;
		}
	
	// connect on socket
		if((connect(*ap_socket, (struct sockaddr *)&serverAddr, sizeof(struct sockaddr))) < 0)
		{
			perror("service_create()");
			close(*ap_socket);
			return false;
		}
	
	return true;
}

Boolean session_create(const int a_socket)
{
	// variables
		Message msgOut, msgIn;
		
	// init vars
		Message_clear(&msgOut);
		Message_clear(&msgIn);
		
	// session challenge dialogue
	
		// cilent: greeting
		// server: identify
			Message_setType(&msgOut, SIFTP_VERBS_SESSION_BEGIN);
			
			if(!service_query(a_socket, &msgOut, &msgIn) || !Message_hasType(&msgIn, SIFTP_VERBS_IDENTIFY))
			{
				fprintf(stderr, "session_create(): connection request rejected.\n");
				return false;
			}
			
		// cilent: username
		// server: accept|deny
			Message_setType(&msgOut, SIFTP_VERBS_USERNAME);
			
			if(!service_query(a_socket, &msgOut, &msgIn) || !Message_hasType(&msgIn, SIFTP_VERBS_ACCEPTED))
			{
				fprintf(stderr, "session_create(): username rejected.\n");
				return false;
			}
		
		// cilent: password
		// server: accept|deny
		
			Message_setType(&msgOut, SIFTP_VERBS_PASSWORD);
			
			// get user input
				printf("\npassword: ");
				scanf("%s", msgOut.m_param);
		
			if(!service_query(a_socket, &msgOut, &msgIn) || !Message_hasType(&msgIn, SIFTP_VERBS_ACCEPTED))
			{
				fprintf(stderr, "session_create(): password rejected.\n");
				return false;
			}
		
		// session now established
			#ifndef NODEBUG
				printf("session_create(): success\n");
			#endif
			
	return true;
}

void service_loop(const int a_socket)
{
	// variables
		char buf[SIFTP_MESSAGE_SIZE+1];
		Boolean status, isLooping = true;
		String bufCR;
		
	while(isLooping)
	{
		// read input
			printf("\nSimpleFTP> ");
			memset(&buf, 0, sizeof(buf));
			fgets(buf, sizeof(buf), stdin);
			
			// remove newline
			if((bufCR = strrchr(buf, '\n')) != NULL)
				*bufCR = '\0';
			
			#ifndef NODEBUG
				printf("\nservice_loop(): got command {str='%s',arg='%s'}\n", buf, strchr(buf, ' '));
			#endif
			
		// handle commands
			status = true;
			
			if(strstr(buf, "exit"))
			{
				isLooping = false;
			}
			else if(strstr(buf, "help"))
			{
				printf("\nls\n  displays contents of remote current working directory.\n");
				printf("\nlls\n  displays contents of local current working directory.\n");
				printf("\npwd\n  displays path of remote current working directory.\n");
				printf("\nlpwd\n  displays path of local current working directory.\n");
				printf("\ncd <path>\n  changes the remote current working directory to the specified <path>.\n");
				printf("\nlcd <path>\n  changes the local current working directory to the specified <path>.\n");
				printf("\nget <src> [dest]\n  downloads the remote <src> to local current working directory by the name of [dest] if specified.\n");
				printf("\nput <src> [dest]\n  uploads the local <src> file to remote current working directory by the name of [dest] if specified.\n");
				printf("\nhelp\n  displays this message.\n");
				printf("\nexit\n  terminates this program.\n");
			}
			else if(strlen(buf) > 0)
			{
				status = service_handleCmd(a_socket, buf);
			}
			else
				continue;
			
			
		// display command status
			printf("\n(status) %s.", (status ? "Success" : "Failure"));
	}
}

Boolean service_handleCmd(const int a_socket, const String a_cmdStr)
{
	// variables
		Message msgOut, msgIn;
		
		char cmdName[MODEL_COMMAND_SIZE+1];
		String cmdArg, dataBuf;
		int dataBufLen;
		
		Boolean tempStatus;
		
	// init variables
		Message_clear(&msgOut);
		Message_clear(&msgIn);
		
		// command name
			memset(cmdName, 0, sizeof(cmdName));
			strncpy(cmdName, a_cmdStr, MODEL_COMMAND_SIZE);
		
		// argument string
			cmdArg = service_handleCmd_getNextArg(a_cmdStr);
		
	if(strstr(cmdName, "lls"))
	{
		if((dataBuf = service_handleCmd_readDir(g_pwd, &dataBufLen)) != NULL)
		{
			printf("%s", dataBuf);
			free(dataBuf);
			
			return true;
		}
	}
	
	else if(strstr(cmdName, "lpwd"))
	{
		printf("%s", g_pwd);
		return true;
	}
	
	else if(strstr(cmdName, "lcd"))
	{
		return service_handleCmd_chdir(a_cmdStr, cmdArg, g_pwd);
	}
	
	else if(strstr(cmdName, "ls") || strstr(cmdName, "pwd") || strstr(cmdName, "cd"))
	{
		// build command
			Message_setType(&msgOut, SIFTP_VERBS_COMMAND);
			Message_setValue(&msgOut, a_cmdStr);
		
		// perform command
			if(remote_exec(a_socket, &msgOut))
			{
				if(strstr(cmdName, "cd") != NULL) // no output
					return true;
				
				else
				{
					if((dataBuf = siftp_recvData(a_socket, &dataBufLen)) != NULL)
					{
						printf("%s", dataBuf);
						free(dataBuf);
						
						return true;
					}
				}
			}
	}
	
	else if(strstr(cmdName, "get") && cmdArg != NULL)
	{
		String destFn;
		char destPath[PATH_MAX+1];
		
		// init vars
			if((destFn = service_handleCmd_getNextArg(cmdArg)) == NULL)
				destFn = cmdArg;
			
			tempStatus = false;
		
		// build command
			Message_setType(&msgOut, SIFTP_VERBS_COMMAND);
			Message_setValue(&msgOut, a_cmdStr);
			
		// perform command
			if(remote_exec(a_socket, &msgOut))
			{
				if((dataBuf = siftp_recvData(a_socket, &dataBufLen)) != NULL)
				{
					// determine destination file path
					if(service_getAbsolutePath(g_pwd, destFn, destPath))
					{
						tempStatus = service_handleCmd_writeFile(destPath, dataBuf, dataBufLen-1);
					}
					
					free(dataBuf);
				}
			}
			
		return tempStatus;
	}
	
	return false;
}
