#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <signal.h>
#include <iostream>
#include <stdlib.h>
#include <vector>
#include <string>
#include <string.h>
#include <sstream>
#include <map>
#include <unordered_map>
#include <utility>
#include <unistd.h>
#include <regex>
#include <fcntl.h>
#include <arpa/inet.h>
#include <queue>
#include <dirent.h>

#define MAX_MESSAGE_LEN 1024
#define MAX_NAME_LEN 20
#define MAX_CLIENT_NUM 30
#define CLIENT_SHM_KEY 8888
#define PID_SHM_KEY 8787
#define MESSAGE_SHM_KEY 7777
#define SENDER_SHM_KEY 6666
#define PERMS 0666

using namespace std;

vector< vector<string> > commands;
vector<string> mediums;
vector< pair<int, int> > user_pipes;
unsigned int lineIdx = 0;
unordered_map<unsigned int, int *> pipes;
vector<int *> ordinary_pipes;
unsigned int user_id;

// Shared memory region (beginning)

struct client_info{
	unsigned int id;
	char name[MAX_NAME_LEN+1];
	char ip[INET_ADDRSTRLEN];
	in_port_t port;
	bool isFifoExists[MAX_CLIENT_NUM+1];
};
struct client_info *clients;
int *pid_table;
// For broadcast, tell or yell messages
char *recvMessage;
unsigned int *sender;

// Shared memory region (end)

int null_fd, sendFd;
int recvFds[MAX_CLIENT_NUM+1];
int client_shm_id, pid_shm_id, message_shm_id, sender_shm_id;

regex redi_file(">"), ord_pipe("[|]"), num_pipe("[|]([0-9]{1,4})"), both_pipe("!([0-9]{1,4})");
regex send_pipe("[>]([0-9]{1,4})"), recv_pipe("[<]([0-9]{1,4})");
regex all(">|[|]|[|]([0-9]{1,4})|!([0-9]{1,4})"), new_line_pipe("[|]([0-9]{1,4})|!([0-9]{1,4})");

void initSharedMemory(){
	client_shm_id = shmget(CLIENT_SHM_KEY, (MAX_CLIENT_NUM+1)*sizeof(client_info), IPC_CREAT | IPC_EXCL | PERMS);
	pid_shm_id = shmget(PID_SHM_KEY, (MAX_CLIENT_NUM+1)*sizeof(unsigned int), IPC_CREAT | IPC_EXCL | PERMS);
	message_shm_id = shmget(MESSAGE_SHM_KEY, 3*(MAX_MESSAGE_LEN+1)*sizeof(char), IPC_CREAT | IPC_EXCL | PERMS);
	sender_shm_id = shmget(SENDER_SHM_KEY, sizeof(unsigned int), IPC_CREAT | IPC_EXCL | PERMS);
	pid_table = (int *)shmat(pid_shm_id, (char *) 0, 0);
	for(int i=1;i<=MAX_CLIENT_NUM;i++){
		pid_table[i] = -1;
	}
	shmdt(pid_table);
}

unsigned int drawTicket(){
	unsigned int number;
	pid_table = (int *)shmat(pid_shm_id, (char *) 0, 0);
	for(unsigned int i=1;i<=MAX_CLIENT_NUM;i++){
		if(pid_table[i] == -1){
			number = i;
			break;
		}
	}
	shmdt(pid_table);
	return number;
}

void returnTicket(unsigned int number){
	pid_table[number] = -1;
}

void signalHandler(int signum){
	int status;
	// wait for any child
	waitpid(-1, &status, 0);
}

void broadcast(int signum){
	printf("%s", recvMessage);
}

void killChild(int signum){
	pid_table = (int *)shmat(pid_shm_id, (char *) 0, 0);
	for(int i=1;i<=MAX_CLIENT_NUM;i++){
		if(pid_table[i] != -1) kill(pid_table[i], SIGINT);
	}
	DIR *dp = opendir("./user_pipe/");
	struct dirent *entry;
	while((entry = readdir(dp))){
		char filename[300];
		sprintf(filename, "./user_pipe/%s", entry->d_name);
		unlink(filename);
	}
	shmdt(pid_table);
	shmctl(client_shm_id, IPC_RMID, (struct shmid_ds *) 0);
	shmctl(pid_shm_id, IPC_RMID, (struct shmid_ds *) 0);
	shmctl(message_shm_id, IPC_RMID, (struct shmid_ds *) 0);
	shmctl(sender_shm_id, IPC_RMID, (struct shmid_ds *) 0);
	exit(0);
}

void beKilled(int signum){
	shmdt(clients);
	shmdt(pid_table);
	shmdt(recvMessage);
	shmdt(sender);
	exit(0);
}

void fifoName(char *filename, unsigned int sender, unsigned int receiver){
	sprintf(filename, "./user_pipe/fifo_%u_%u", sender, receiver);
}

void openForRead(int signum){
	char filename[20];
	fifoName(filename, *sender, user_id);
	recvFds[*sender] = open(filename, O_RDONLY);
}

void who(unsigned int id){
	printf("<ID>\t<nickname>\t<IP:port>\t<indicate me>\n");
	for(unsigned int i=1;i<=MAX_CLIENT_NUM;i++){
		if(pid_table[i] == -1) continue;
		if(id == i)
			printf("%u\t%s\t%s:%i\t<-me\n", i, clients[i].name, clients[i].ip, ntohs(clients[i].port));
		else
			printf("%u\t%s\t%s:%i\n", i, clients[i].name, clients[i].ip, ntohs(clients[i].port));
	}
}

void tell(unsigned int sender, unsigned int receiver, const char *message){
	sprintf(recvMessage, "*** %s told you ***: %s\n", clients[sender].name, message);
	if(pid_table[receiver] != -1){
		kill(pid_table[receiver], SIGUSR1);
	}
	else{
		printf("*** Error: user #%u does not exist yet. ***\n", receiver);
	}
}

void yell(unsigned int id, const char *message){
	sprintf(recvMessage, "*** %s yelled ***: %s\n", clients[id].name, message);
	printf("%s", recvMessage);
	for(unsigned int i=1;i<=MAX_CLIENT_NUM;i++){
		if(pid_table[i] == -1 || i == id) continue;
		kill(pid_table[i], SIGUSR1);
	}
}

void rename(unsigned int id, const char *name){
	bool collision = false;
	for(int i=1;i<=MAX_CLIENT_NUM;i++){
		if(strcmp(clients[i].name, name) == 0){
			collision = true;
			break;
		}
	}
	if(collision){
		printf("*** User '%s' already exists. ***\n", name);
	}
	else{
		strncpy(clients[id].name, name, MAX_NAME_LEN+1);
		sprintf(recvMessage, "*** User from %s:%i is named '%s'. ***\n", clients[id].ip, ntohs(clients[id].port), name);
		printf("%s", recvMessage);
		for(unsigned int i=1;i<=MAX_CLIENT_NUM;i++){
			if(pid_table[i] == -1 || i == id) continue;
			kill(pid_table[i], SIGUSR1);
		}
	}
}

void login(unsigned int id, int fd, sockaddr_in *cli_addr){
	// initialize
	client_shm_id = shmget(CLIENT_SHM_KEY, (MAX_CLIENT_NUM+1)*sizeof(client_info), 0);
	pid_shm_id = shmget(PID_SHM_KEY, (MAX_CLIENT_NUM+1)*sizeof(unsigned int), 0);
	message_shm_id = shmget(MESSAGE_SHM_KEY, 3*(MAX_MESSAGE_LEN+1)*sizeof(char), 0);
	sender_shm_id = shmget(SENDER_SHM_KEY, sizeof(unsigned int), 0);
	clients = (struct client_info *)shmat(client_shm_id, (char *) 0, 0);
	pid_table = (int *)shmat(pid_shm_id, (char *) 0, 0);
	recvMessage = (char *)shmat(message_shm_id, (char *) 0, 0);
	sender = (unsigned int *)shmat(sender_shm_id, (char *) 0, 0);
	clients[id].id = id;
	strncpy(clients[id].name, "(no name)", MAX_NAME_LEN+1);
	inet_ntop(AF_INET, &(cli_addr->sin_addr.s_addr), clients[id].ip, INET_ADDRSTRLEN);
	clients[id].port = cli_addr->sin_port;
	for(int i=1;i<=MAX_CLIENT_NUM;i++){
		clients[id].isFifoExists[i] = false;
	}
	pid_table[id] = getpid();
	user_id = id;
	signal(SIGCHLD, signalHandler);
	signal(SIGUSR1, broadcast);
	signal(SIGUSR2, openForRead);
	signal(SIGINT, beKilled);

	sprintf(recvMessage, "*** User '%s' entered from %s:%i. ***\n", clients[id].name, clients[id].ip, ntohs(clients[id].port));
	dprintf(fd, "****************************************\n** Welcome to the information server. **\n****************************************\n");
	dprintf(fd, "%s", recvMessage);
	for(unsigned int i=1;i<=MAX_CLIENT_NUM;i++){
		if(pid_table[i] == -1 || i == id) continue;
		kill(pid_table[i], SIGUSR1);
	}
}

void logout(unsigned int id){
	sprintf(recvMessage, "*** User '%s' left. ***\n", clients[id].name);
	for(unsigned int i=1;i<=MAX_CLIENT_NUM;i++){
		if(pid_table[i] == -1 || i == id) continue;
		kill(pid_table[i], SIGUSR1);
	}
	returnTicket(id);
}

int parseLine(const string &line){
	stringstream ss(line);
	string s;
	commands.clear();
	mediums.clear();
	user_pipes.clear();
	commands.push_back(vector<string>());
	int in = -1, out = -1;
	while(ss>>s){
		if(regex_match(s, all)){
			mediums.push_back(s);
			commands.push_back(vector<string>());
			user_pipes.emplace_back(in, out);
			in = out = -1;
		}
		else if(regex_match(s, recv_pipe)){
			in = stoul(s.substr(1));
		}
		else if(regex_match(s, send_pipe)){
			out = stoul(s.substr(1));
		}
		else{
			commands.back().push_back(s);
		}
	}
	if(commands.back().size() == 0) commands.pop_back();
	else user_pipes.emplace_back(in, out);
	return commands.size();
}

void shell_service(int fd, unsigned int id){
	dup2(fd, STDIN_FILENO);
	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);
	
	// initialize PATH to "bin:."
	setenv("PATH", "bin:.", 1);

	printf("%% ");
	char *line = NULL;
	size_t len = 0;
	FILE *fp = fdopen(fd, "r");
	while(1){
		if(getline(&line, &len, fp) < 0) continue;
		line[strcspn(line, "\r\n")] = 0;
		string lineStr(line);
		int n = parseLine(lineStr);
		if(n == 0){
			printf("%% ");
			return;
		}
		if(commands[0][0] == "setenv"){
			setenv(commands[0][1].c_str(), commands[0][2].c_str(), 1);
		}
		else if(commands[0][0] == "printenv"){
			char *p;
			if((p = getenv(commands[0][1].c_str())) != NULL)
				printf("%s\n", p);
		}
		else if(commands[0][0] == "who"){
			who(id);
		}
		else if(commands[0][0] == "tell"){
			string message = "";
			if(commands[0].size() > 2) message = lineStr.substr(lineStr.find(commands[0][2],0));
			else if(mediums.size() > 0) message = lineStr.substr(lineStr.find(mediums[0],0));
			tell(id, stoul(commands[0][1]), message.c_str());
		}
		else if(commands[0][0] == "yell"){
			string message = "";
			if(commands[0].size() > 1) message = lineStr.substr(lineStr.find(commands[0][1],0));
			else if(mediums.size() > 0) message = lineStr.substr(lineStr.find(mediums[0],0));
			yell(id, message.c_str());
		}
		else if(commands[0][0] == "name"){
			rename(id, commands[0][1].c_str());
		}
		else if(commands[0][0] == "exit"){
			logout(id);
			shmdt(clients);
			shmdt(pid_table);
			shmdt(recvMessage);
			shmdt(sender);
			break;
		}
		else{
			pid_t childPid, lastWaitChild = 0;
			for(int i=0;i<n;i++){
				int m = mediums.size();
				// file is already redirected
				if(i > 0 && regex_match(mediums[i-1], redi_file)) continue;
				// output to ordinary pipe
				if(m >= i+1 && regex_match(mediums[i], ord_pipe)){
					ordinary_pipes.push_back(new int[2]);
					pipe(ordinary_pipes.back());
				}
				// output/stderr to numbered pipe
				if(m >= i+1 && regex_match(mediums[i], new_line_pipe)){
					int offset = stoi(mediums[i].substr(1));
					std::unordered_map<unsigned int, int *>::iterator it = pipes.find(lineIdx+offset);
					if(it == pipes.end()){
						pipes[lineIdx+offset] = new int[2];
						pipe(pipes[lineIdx+offset]);
					}
				}
				// input from user pipe
				bool recvPipeError = false;
				bool recvPipeSucceed = false;
				char recvName[20];
				if(user_pipes[i].first != -1){
					fifoName(recvName, user_pipes[i].first, id);
					if(pid_table[user_pipes[i].first] == -1){
						printf("*** Error: user #%u does not exist yet. ***\n", user_pipes[i].first);
						recvPipeError = true;
					}
					else if(!clients[id].isFifoExists[user_pipes[i].first]){
						printf("*** Error: the pipe #%u->#%u does not exist yet. ***\n", user_pipes[i].first, id);
						recvPipeError = true;
					}
					else{
						recvPipeSucceed = true;
						sprintf(recvMessage, "*** %s (#%u) just received from %s (#%u) by '%s' ***\n", clients[id].name, id, clients[user_pipes[i].first].name, user_pipes[i].first, line);
						clients[id].isFifoExists[user_pipes[i].first] = false;
					}
				}
				// output to user pipe
				bool sendPipeError = false;
				bool sendPipeSucceed = false;
				char sendName[20];
				if(user_pipes[i].second != -1){
					fifoName(sendName, id, user_pipes[i].second);
					if(pid_table[user_pipes[i].second] == -1){
						printf("*** Error: user #%u does not exist yet. ***\n", user_pipes[i].second);
						sendPipeError = true;
					}
					else if(clients[user_pipes[i].second].isFifoExists[id]){
						printf("*** Error: the pipe #%u->#%u already exists. ***\n", id, user_pipes[i].second);
						sendPipeError = true;
					}
					else{
						sendPipeSucceed = true;
						if(recvPipeSucceed)
							sprintf(recvMessage+strlen(recvMessage), "*** %s (#%u) just piped '%s' to %s (#%u) ***\n", clients[id].name, id, line, clients[user_pipes[i].second].name, user_pipes[i].second);
						else
							sprintf(recvMessage, "*** %s (#%u) just piped '%s' to %s (#%u) ***\n", clients[id].name, id, line, clients[user_pipes[i].second].name, user_pipes[i].second);
						clients[user_pipes[i].second].isFifoExists[id] = true;
					}
				}
				if(recvPipeSucceed || sendPipeSucceed){
					printf("%s", recvMessage);
					for(unsigned int k=1;k<=MAX_CLIENT_NUM;k++){
						if(pid_table[k] == -1 || k == id) continue;
						kill(pid_table[k], SIGUSR1);
					}
				}
				// wait until fork succeed
				while((childPid = fork()) < 0){
					int status;
					wait(&status);
				}

				if(childPid == 0){
					dup2(fd, STDIN_FILENO);
					dup2(fd, STDOUT_FILENO);
					dup2(fd, STDERR_FILENO);
				}

				// input from ordinary pipe
				if(i > 0 && regex_match(mediums[i-1], ord_pipe)){
					// parent
					if(childPid > 0){
						close(ordinary_pipes[0][0]);
						close(ordinary_pipes[0][1]);
						delete []ordinary_pipes.front();
						ordinary_pipes.erase(ordinary_pipes.begin());
					}
					// child
					else if(childPid == 0){
						close(ordinary_pipes[0][1]);
						dup2(ordinary_pipes[0][0], STDIN_FILENO);
						close(ordinary_pipes[0][0]);
						delete []ordinary_pipes.front();
						ordinary_pipes.erase(ordinary_pipes.begin());
					}
				}
				// input from numbered pipe
				std::unordered_map<unsigned int, int *>::iterator it = pipes.find(lineIdx);
				if((i == 0 || regex_match(mediums[i-1], new_line_pipe)) && !pipes.empty() && it != pipes.end()){
					// parent
					if(childPid > 0){
						close(it->second[0]);
						close(it->second[1]);
						delete [] it->second;
						pipes.erase(it);
					}
					// child
					else if(childPid == 0){
						close(it->second[1]);
						dup2(it->second[0], STDIN_FILENO);
						close(it->second[0]);
					}
				}
				// input from user pipe
				if(childPid == 0 && user_pipes[i].first != -1){
					if(recvPipeError){
						dup2(null_fd, STDIN_FILENO);
					}
					else{
						dup2(recvFds[user_pipes[i].first], STDIN_FILENO);
					}
				}
				// output to file
				if(m >= i+1 && regex_match(mediums[i], redi_file) && childPid == 0){
					int fd = open(commands[i+1][0].c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0644);
					dup2(fd, STDOUT_FILENO);
				}
				// output to ordinary pipe
				if(m >= i+1 && regex_match(mediums[i], ord_pipe) && childPid == 0){
					close(ordinary_pipes[0][0]);
					dup2(ordinary_pipes[0][1], STDOUT_FILENO);
					close(ordinary_pipes[0][1]);
				}
				// output to numbered pipe
				if(m >= i+1 && regex_match(mediums[i], num_pipe) && childPid == 0){
					int offset = stoi(mediums[i].substr(1));
					std::unordered_map<unsigned int, int *>::iterator it = pipes.find(lineIdx+offset);
					close(it->second[0]);
					dup2(it->second[1], STDOUT_FILENO);
					close(it->second[1]);
				}
				// output & stderr to pipe
				if(m >= i+1 && regex_match(mediums[i], both_pipe) && childPid == 0){
					int offset = stoi(mediums[i].substr(1));
					std::unordered_map<unsigned int, int *>::iterator it = pipes.find(lineIdx+offset);
					close(it->second[0]);
					dup2(it->second[1], STDOUT_FILENO);
					dup2(it->second[1], STDERR_FILENO);
					close(it->second[1]);
				}
				// output to user pipe
				if(childPid == 0 && user_pipes[i].second != -1){
					if(sendPipeError){
						dup2(null_fd, STDOUT_FILENO);
					}
					else{
						mknod(sendName, S_IFIFO | PERMS, 0);
						*sender = id;
						kill(pid_table[user_pipes[i].second], SIGUSR2);
						sendFd = open(sendName, O_WRONLY);
						dup2(sendFd, STDOUT_FILENO);
					}
				}
				if(i == m || regex_match(mediums[i], redi_file)) lastWaitChild = childPid;
				//child process set up argv and run execvp
				if(childPid == 0){
					char ** argv = new char *[commands[i].size()+1];
					for(size_t j=0;j<commands[i].size()+1;j++){
						if(j == commands[i].size()) argv[j] = NULL;
						else argv[j] = (char *)commands[i][j].c_str();
					}
					if(execvp(argv[0], argv) < 0){
						fprintf(stderr, "Unknown command: [%s].\n", argv[0]);
						exit(-1);
					}
				}
				if(childPid > 0 && n > i+1 && regex_match(mediums[i], new_line_pipe)){
					lineIdx++;
				}
			}
			int status;
			// wait for last child to finish
			if(lastWaitChild) waitpid(lastWaitChild, &status, 0);
		}
		lineIdx++;
		printf("%% ");
	}
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
	fclose(fp);
	close(fd);
}

int main(int argc, char *argv[], char **envp){
	// initialize
	initSharedMemory();
	null_fd = open("/dev/null", O_RDWR);
	// signal(SIGCHLD, reaper);
	signal(SIGINT, killChild);
	signal(SIGCHLD, signalHandler);
	setbuf(stdout, NULL);

	u_short portNumber;
	portNumber = atoi(argv[1]);
	struct sockaddr_in cli_addr, serv_addr;
	int sockfd, newsockfd, clilen = sizeof(cli_addr);
	// connect a socket file descriptor with given struct sockaddr
	sockfd = socket(AF_INET /* ipv4 */, SOCK_STREAM /* TCP */, 0);
	int optval = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(portNumber);
	bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
	listen(sockfd, 30);
	while(1){
		if((newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, (socklen_t *)&clilen))<0){
			// SIGCHLD signal
			continue;
		}
		unsigned int number = drawTicket();
		pid_t pid = fork();
		if(pid > 0){
			close(newsockfd);
		}
		else if(pid == 0){
			close(sockfd);
			login(number, newsockfd, &cli_addr);
			shell_service(newsockfd, number);
			return(0);
		}
	}
	killChild(0);
	return 0;
}