#include <sys/types.h>
#include <sys/wait.h>
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

using namespace std;

void signalHandler(int signum){
	int status;
	// wait for any child
	waitpid(-1, &status, 0);
}

vector< vector<string> > commands;
vector<string> mediums;
vector< pair<int, int> > user_pipes;

unordered_map<string, string> init_envp;
struct client_info{
	unsigned int id;
	string name;
	struct sockaddr_in *address;
	unordered_map<string, string> envs;
	int fd;
	unsigned int lineIdx;
	unordered_map<unsigned int, int *> pipes;
	vector<int *> ordinary_pipes;
	unordered_map<unsigned int, int *> recvMessages;
	client_info(unsigned int i) : id(i), name("(no name)"), lineIdx(0){
		for(auto k : init_envp){
			envs[k.first] = k.second;
		}
		address = new struct sockaddr_in;
	}
	~client_info(){
		delete address;
	}
};
map<unsigned int, struct client_info *> clients;
unordered_map<unsigned int, unsigned int> fd_table;
priority_queue<unsigned int, vector<unsigned int>, greater<unsigned int> > tickets;
fd_set rfds, afds;
int null_fd;

regex redi_file(">"), ord_pipe("[|]"), num_pipe("[|]([0-9]{1,4})"), both_pipe("!([0-9]{1,4})");
regex send_pipe("[>]([0-9]{1,4})"), recv_pipe("[<]([0-9]{1,4})");
regex all(">|[|]|[|]([0-9]{1,4})|!([0-9]{1,4})"), new_line_pipe("[|]([0-9]{1,4})|!([0-9]{1,4})");
regex number("([0-9]{1,4})");

void initEnvp(char **envp){
	for(char **env=envp;*env!=NULL;env++){
		string tmp(*env);
		int equalIdx = tmp.find("=");
		string key = tmp.substr(0, equalIdx), value = tmp.substr(equalIdx+1);
		init_envp[key] = value;
	}
	init_envp["PATH"] = "bin:.";
}

void initTickets(){
	for(unsigned int i=1;i<=30;i++){
		tickets.push(i);
	}
}

unsigned int drawTicket(){
	unsigned int number = tickets.top();
	tickets.pop();
	return number;
}

void returnTicket(unsigned int number){
	tickets.push(number);
}

void who(unsigned int id, int fd){
	dprintf(fd, "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n");
	for(auto c : clients){
		char ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &(c.second->address->sin_addr.s_addr), ip, INET_ADDRSTRLEN);
		if(id == c.first)
			dprintf(fd, "%u\t%s\t%s:%i\t<-me\n", c.first, c.second->name.c_str(), ip, ntohs(c.second->address->sin_port));
		else
			dprintf(fd, "%u\t%s\t%s:%i\n", c.first, c.second->name.c_str(), ip, ntohs(c.second->address->sin_port));
	}
}

void tell(unsigned int sender, unsigned int receiver, const char *message){
	if(clients.find(receiver) != clients.end()){
		dprintf(clients[receiver]->fd, "*** %s told you ***: %s\n", clients[sender]->name.c_str(), message);
	}
	else{
		dprintf(clients[sender]->fd, "*** Error: user #%u does not exist yet. ***\n", receiver);
	}
}

void yell(unsigned int id, const char *message){
	for(auto c : clients){
		dprintf(c.second->fd, "*** %s yelled ***: %s\n", clients[id]->name.c_str(), message);
	}
}

void rename(unsigned int id, string &name){
	bool collision = false;
	for(auto c : clients){
		if(c.second->name == name){
			collision = true;
			break;
		}
	}
	if(collision){
		dprintf(clients[id]->fd, "*** User '%s' already exists. ***\n", name.c_str());
	}
	else{
		clients[id]->name = name;
		char ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &(clients[id]->address->sin_addr.s_addr), ip, INET_ADDRSTRLEN);
		for(auto c : clients){
			dprintf(c.second->fd, "*** User from %s:%i is named '%s'. ***\n", ip, ntohs(clients[id]->address->sin_port), name.c_str());
		}
	}
}

void login(unsigned int id){
	dprintf(clients[id]->fd, "****************************************\n** Welcome to the information server. **\n****************************************\n");
	char ip[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(clients[id]->address->sin_addr.s_addr), ip, INET_ADDRSTRLEN);
	for(auto c : clients){
		dprintf(c.second->fd, "*** User '%s' entered from %s:%i. ***\n", clients[id]->name.c_str(), ip, ntohs(clients[id]->address->sin_port));
	}
}

void logout(unsigned int id){
	for(auto c : clients){
		if(c.second->id != id)
			dprintf(c.second->fd, "*** User '%s' left. ***\n", clients[id]->name.c_str());
	}
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
	// clear old environment variable and set an new one
	clearenv();
	for(auto env : clients[id]->envs){
		setenv(env.first.c_str(), env.second.c_str(), 1);
	}

	// string line;
	char *line = NULL;
	size_t len = 0;
	FILE *fp = fdopen(fd, "r");
	getline(&line, &len, fp);
	line[strcspn(line, "\r\n")] = 0;
	string lineStr(line);
	int n = parseLine(lineStr);
	if(n == 0){
		dprintf(fd, "%% ");
		return;
	}
	if(commands[0][0] == "setenv"){
		clients[id]->envs[commands[0][1]] = commands[0][2];
		setenv(commands[0][1].c_str(), commands[0][2].c_str(), 1);
	}
	else if(commands[0][0] == "printenv"){
		char *p;
		if((p = getenv(commands[0][1].c_str())) != NULL)
			dprintf(fd, "%s\n", p);
	}
	else if(commands[0][0] == "who"){
		who(id, fd);
	}
	else if(commands[0][0] == "tell"){
		string message = "";
		if(commands[0].size() > 2) message = lineStr.substr(lineStr.find(commands[0][2],0));
		else if(mediums.size() > 0) message = lineStr.substr(lineStr.find(mediums[0],0));
		if(regex_match(commands[0][1], number))
			tell(id, stoul(commands[0][1]), message.c_str());
		else{
			for(auto c : clients){
				if(commands[0][1] == c.second->name){
					tell(id, c.second->id, message.c_str());
					break;
				}
			}
		}
	}
	else if(commands[0][0] == "yell"){
		string message = "";
		if(commands[0].size() > 1) message = lineStr.substr(lineStr.find(commands[0][1],0));
		else if(mediums.size() > 0) message = lineStr.substr(lineStr.find(mediums[0],0));
		yell(id, message.c_str());
	}
	else if(commands[0][0] == "name"){
		rename(id, commands[0][1]);
	}
	else if(commands[0][0] == "exit"){
		logout(id);
		fclose(fp);
		close(fd);
		delete clients[id];
		clients.erase(id);
		fd_table.erase(fd);
		returnTicket(id);
		FD_CLR(fd, &afds);
		return;
	}

	else{
		pid_t childPid, lastWaitChild = 0;
		for(int i=0;i<n;i++){
			int m = mediums.size();
			// file is already redirected
			if(i > 0 && regex_match(mediums[i-1], redi_file)) continue;
			// output to ordinary pipe
			if(m >= i+1 && regex_match(mediums[i], ord_pipe)){
				clients[id]->ordinary_pipes.push_back(new int[2]);
				pipe(clients[id]->ordinary_pipes.back());
			}
			// output/stderr to numbered pipe
			if(m >= i+1 && regex_match(mediums[i], new_line_pipe)){
				int offset = stoi(mediums[i].substr(1));
				std::unordered_map<unsigned int, int *>::iterator it = clients[id]->pipes.find(clients[id]->lineIdx+offset);
				if(it == clients[id]->pipes.end()){
					clients[id]->pipes[clients[id]->lineIdx+offset] = new int[2];
					pipe(clients[id]->pipes[clients[id]->lineIdx+offset]);
				}
			}
			// input from user pipe
			bool recvPipeError = false;
			if(user_pipes[i].first != -1){
				if(clients.find(user_pipes[i].first) == clients.end()){
					dprintf(fd, "*** Error: user #%u does not exist yet. ***\n", user_pipes[i].first);
					recvPipeError = true;
				}
				else if(clients[id]->recvMessages.find(user_pipes[i].first) == clients[id]->recvMessages.end()){
					dprintf(fd, "*** Error: the pipe #%u->#%u does not exist yet. ***\n", user_pipes[i].first, id);
					recvPipeError = true;
				}
				else{
					for(auto c : clients){
						dprintf(c.second->fd, "*** %s (#%u) just received from %s (#%u) by '%s' ***\n", clients[id]->name.c_str(), id, clients[user_pipes[i].first]->name.c_str(), user_pipes[i].first, line);
					}
				}
			}
			// output to user pipe
			bool sendPipeError = false;
			if(user_pipes[i].second != -1){
				if(clients.find(user_pipes[i].second) == clients.end()){
					dprintf(fd, "*** Error: user #%u does not exist yet. ***\n", user_pipes[i].second);
					sendPipeError = true;
				}
				else if(clients[user_pipes[i].second]->recvMessages.find(id) != clients[user_pipes[i].second]->recvMessages.end()){
					dprintf(fd, "*** Error: the pipe #%u->#%u already exists. ***\n", id, user_pipes[i].second);
					sendPipeError = true;
				}
				else{
					clients[user_pipes[i].second]->recvMessages[id] = new int[2];
					pipe(clients[user_pipes[i].second]->recvMessages[id]);
					for(auto c : clients){
						dprintf(c.second->fd, "*** %s (#%u) just piped '%s' to %s (#%u) ***\n", clients[id]->name.c_str(), id, line, clients[user_pipes[i].second]->name.c_str(), user_pipes[i].second);
					}
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
					close(clients[id]->ordinary_pipes[0][0]);
					close(clients[id]->ordinary_pipes[0][1]);
					delete []clients[id]->ordinary_pipes.front();
					clients[id]->ordinary_pipes.erase(clients[id]->ordinary_pipes.begin());
				}
				// child
				else if(childPid == 0){
					close(clients[id]->ordinary_pipes[0][1]);
					dup2(clients[id]->ordinary_pipes[0][0], STDIN_FILENO);
					close(clients[id]->ordinary_pipes[0][0]);
					delete []clients[id]->ordinary_pipes.front();
					clients[id]->ordinary_pipes.erase(clients[id]->ordinary_pipes.begin());
				}
			}
			// input from numbered pipe
			std::unordered_map<unsigned int, int *>::iterator it = clients[id]->pipes.find(clients[id]->lineIdx);
			if((i == 0 || regex_match(mediums[i-1], new_line_pipe)) && !clients[id]->pipes.empty() && it != clients[id]->pipes.end()){
				// parent
				if(childPid > 0){
					close(it->second[0]);
					close(it->second[1]);
					delete [] it->second;
					clients[id]->pipes.erase(it);
				}
				// child
				else if(childPid == 0){
					close(it->second[1]);
					dup2(it->second[0], STDIN_FILENO);
					close(it->second[0]);
				}
			}
			// input from user pipe
			if(user_pipes[i].first != -1){
				if(recvPipeError){
					// child
					if(childPid == 0){
						dup2(null_fd, STDIN_FILENO);
					}
				}
				else{
					// parent
					if(childPid > 0){
						close(clients[id]->recvMessages[user_pipes[i].first][0]);
						close(clients[id]->recvMessages[user_pipes[i].first][1]);
						delete [] clients[id]->recvMessages[user_pipes[i].first];
						clients[id]->recvMessages.erase(user_pipes[i].first);
					}
					// child
					else if(childPid == 0){
						close(clients[id]->recvMessages[user_pipes[i].first][1]);
						dup2(clients[id]->recvMessages[user_pipes[i].first][0], STDIN_FILENO);
						close(clients[id]->recvMessages[user_pipes[i].first][0]);
					}
				}
			}
			// output to file
			if(m >= i+1 && regex_match(mediums[i], redi_file) && childPid == 0){
				int fd = open(commands[i+1][0].c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0644);
				dup2(fd, STDOUT_FILENO);
			}
			// output to ordinary pipe
			if(m >= i+1 && regex_match(mediums[i], ord_pipe) && childPid == 0){
				close(clients[id]->ordinary_pipes[0][0]);
				dup2(clients[id]->ordinary_pipes[0][1], STDOUT_FILENO);
				close(clients[id]->ordinary_pipes[0][1]);
			}
			// output to numbered pipe
			if(m >= i+1 && regex_match(mediums[i], num_pipe) && childPid == 0){
				int offset = stoi(mediums[i].substr(1));
				std::unordered_map<unsigned int, int *>::iterator it = clients[id]->pipes.find(clients[id]->lineIdx+offset);
				close(it->second[0]);
				dup2(it->second[1], STDOUT_FILENO);
				close(it->second[1]);
			}
			// output & stderr to pipe
			if(m >= i+1 && regex_match(mediums[i], both_pipe) && childPid == 0){
				int offset = stoi(mediums[i].substr(1));
				std::unordered_map<unsigned int, int *>::iterator it = clients[id]->pipes.find(clients[id]->lineIdx+offset);
				close(it->second[0]);
				dup2(it->second[1], STDOUT_FILENO);
				dup2(it->second[1], STDERR_FILENO);
				close(it->second[1]);
			}
			// output to user pipe
			if(user_pipes[i].second != -1){
				if(sendPipeError){
					// child
					if(childPid == 0) dup2(null_fd, STDOUT_FILENO);
				}
				else{
					// child
					if(childPid == 0){
						close(clients[user_pipes[i].second]->recvMessages[id][0]);
						dup2(clients[user_pipes[i].second]->recvMessages[id][1], STDOUT_FILENO);
						close(clients[user_pipes[i].second]->recvMessages[id][1]);
					}
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
				clients[id]->lineIdx++;
			}
		}
		int status;
		// wait for last child to finish
		if(lastWaitChild) waitpid(lastWaitChild, &status, 0);
	}
	clients[id]->lineIdx++;
	dprintf(fd, "%% ");
}

int main(int argc, char *argv[], char **envp){
	// initialize
	initEnvp(envp);
	initTickets();
	null_fd = open("/dev/null", O_RDWR);
	signal(SIGCHLD, signalHandler);
	setbuf(stdout, NULL);

	u_short portNumber;
	portNumber = atoi(argv[1]);
	struct sockaddr_in cli_addr, serv_addr;
	int sockfd, clilen = sizeof(cli_addr);
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

	int nfds = sockfd+1;
	FD_ZERO(&afds);
	FD_SET(sockfd, &afds);
	while(1){
		memcpy(&rfds, &afds, sizeof(rfds));
		// block until (any fd is ready to be read) or (any client want to connect to socket)
		if(select(nfds, &rfds, (fd_set *)NULL, (fd_set *)NULL, (timeval *)NULL) < 0){
			if (errno == EINTR)	continue;
			printf("select: %s\n", strerror(errno));
			exit(-1);
		}

		for(int fd=0;fd<nfds;fd++){
			if(FD_ISSET(fd, &rfds)){
				// client wants to construct an new connection with server through the socket.
				if(fd == sockfd){
					unsigned int number = drawTicket();
					clients[number] = new struct client_info(number);
					int newsockfd = accept(sockfd, (struct sockaddr *) clients[number]->address, (socklen_t *)&clilen);
					nfds = newsockfd+1>nfds?newsockfd+1:nfds;
					FD_SET(newsockfd, &afds);
					clients[number]->fd = newsockfd;
					fd_table[newsockfd] = number;
					login(number);
					dprintf(newsockfd, "%% ");
				}
				// fd is ready to be read.
				else{
					shell_service(fd, fd_table[fd]);
				}
			}
		}
	}
	return 0;
}