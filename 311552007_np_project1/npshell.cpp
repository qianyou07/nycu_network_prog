#include <sys/types.h>
#include <sys/wait.h>
#include <iostream>
#include <stdlib.h>
#include <vector>
#include <string>
#include <string.h>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <unistd.h>
#include <regex>
#include <fcntl.h>

using namespace std;

void signalHandler(int signum){
	int status;
	// wait for any child
	waitpid(-1, &status, 0);
}

vector< vector<string> > commands;
vector<string> mediums;
unsigned int lineIdx = 0;
unordered_map<unsigned int, int *> pipes;
vector<int *> ordinary_pipes;



regex redi_file(">"), ord_pipe("[|]"), num_pipe("[|]([0-9]{1,4})"), both_pipe("!([0-9]{1,4})");
regex all(">|[|]|[|]([0-9]{1,4})|!([0-9]{1,4})"), new_line_pipe("[|]([0-9]{1,4})|!([0-9]{1,4})");

int parseLine(const string &line){
	stringstream ss(line);
	string s;
	commands.clear();
	mediums.clear();
	commands.push_back(vector<string>());
	while(ss>>s){
		if(regex_match(s, all)){
			mediums.push_back(s);
			commands.push_back(vector<string>());
		}
		else{
			commands.back().push_back(s);
		}
	}
	if(commands.back().size() == 0) commands.pop_back();
	return commands.size();
}

int main(){
	// initialize PATH to "bin:."
	setenv("PATH", "bin:.", 1);
	// when receive SIGCHLD signal, go to signalHandler
	signal(SIGCHLD, signalHandler);

	printf("%% ");
	string line;
	while(getline(cin, line)){
		int n = parseLine(line);
		if(n == 0){
			printf("%% ");
			continue;
		}
		if(n == 1 && commands[0][0] == "setenv"){
			setenv(commands[0][1].c_str(), commands[0][2].c_str(), 1);
		}
		else if(n == 1 && commands[0][0] == "printenv"){
			char *p;
			if((p = getenv(commands[0][1].c_str())) != NULL)
				printf("%s\n", p);
		}
		else if(n == 1 && commands[0][0] == "exit"){
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
				// wait until fork succeed
				while((childPid = fork()) < 0){
					int status;
					wait(&status);
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
				if(i == m || regex_match(mediums[i], redi_file)) lastWaitChild = childPid;
				//child process set up argv and run execvp
				if(childPid == 0){
					char ** argv = new char *[commands[i].size()+1];
					for(int j=0;j<commands[i].size()+1;j++){
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
	return 0;
}