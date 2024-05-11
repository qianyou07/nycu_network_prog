#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

using boost::asio::ip::tcp;
using namespace std;

class session
: public std::enable_shared_from_this<session>
{
public:
	session(tcp::socket socket)
		: socket_(std::move(socket))
	{
	}

	void start()
	{
		do_read();
	}

private:
    void do_read()
    {
        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(data_, max_length),
            [this, self](boost::system::error_code ec, size_t length)
            {
                if (!ec)
                {
                    ParseRequest();
					printInfo();
                    do_request();
                }
            });
    }
    void ParseRequest()
    {
		char filler[50];
		sscanf(data_, "%s %s %s %s %s", REQUEST_METHOD, REQUEST_URI, SERVER_PROTOCOL, filler, HTTP_HOST);
		strcpy(SERVER_ADDR, socket_.local_endpoint().address().to_string().c_str());
		sprintf(SERVER_PORT, "%u", socket_.local_endpoint().port());
		strcpy(REMOTE_ADDR, socket_.remote_endpoint().address().to_string().c_str());
		sprintf(REMOTE_PORT, "%u", socket_.remote_endpoint().port());
		char *pch = strstr(REQUEST_URI, "?");
		if(pch != NULL){
			strncpy(QUERY_STRING, REQUEST_URI+(pch-REQUEST_URI+1), 512);
			strncpy(FILENAME+2, REQUEST_URI+1, pch-REQUEST_URI-1);
		}
		else{
			memset(QUERY_STRING, 0, 512);
			strncpy(FILENAME+2, REQUEST_URI+1, 28);
		}
    }
    void setEnvironVar()
    {
		setenv("REQUEST_METHOD", REQUEST_METHOD, 1);
		setenv("REQUEST_URI", REQUEST_URI, 1);
		setenv("QUERY_STRING", QUERY_STRING, 1);
		setenv("SERVER_PROTOCOL", SERVER_PROTOCOL, 1);
		setenv("HTTP_HOST", HTTP_HOST, 1);
		setenv("SERVER_ADDR", SERVER_ADDR, 1);
		setenv("SERVER_PORT", SERVER_PORT, 1);
		setenv("REMOTE_ADDR", REMOTE_ADDR, 1);
		setenv("REMOTE_PORT", REMOTE_PORT, 1);
    }
	void do_request()
	{
		pid_t pid;
		if((pid = fork()) == 0){
			// child
			setEnvironVar();
			int new_sockfd = socket_.native_handle();
			dup2(new_sockfd, STDIN_FILENO);
			dup2(new_sockfd, STDOUT_FILENO);
			dup2(new_sockfd, STDERR_FILENO);
			close(new_sockfd);
			cout << "HTTP/1.1 200 OK\r\n" ;
			cout << "Content-Type: text/html\r\n" ;
			fflush(stdout);
			execlp(FILENAME, FILENAME, NULL);
		}
		else{
			socket_.close();
		}
		do_read();
	}

	void printInfo(){
		std::cout<<"----------Connection Info----------\n";
		std::cout<<"REQUEST_METHOD: "<<REQUEST_METHOD<<"\n";
		std::cout<<"REQUEST_URI: "<<REQUEST_URI<<"\n";
		std::cout<<"QUERY_STRING: "<<QUERY_STRING<<"\n";
		std::cout<<"SERVER_PROTOCOL: "<<SERVER_PROTOCOL<<"\n";
		std::cout<<"HTTP_HOST: "<<HTTP_HOST<<"\n";
		std::cout<<"SERVER_ADDR: "<<SERVER_ADDR<<"\n";
		std::cout<<"SERVER_PORT: "<<SERVER_PORT<<"\n";
		std::cout<<"REMOTE_ADDR: "<<REMOTE_ADDR<<"\n";
		std::cout<<"REMOTE_PORT: "<<REMOTE_PORT<<"\n";
		std::cout<<"FILENAME: "<<FILENAME<<"\n";
		std::cout<<"------------End of info------------\n";
	}

	tcp::socket socket_;
	enum { max_length = 1024 };
	char data_[max_length];
	char REQUEST_METHOD[20];
	char REQUEST_URI[1024];
	char QUERY_STRING[512];
	char SERVER_PROTOCOL[10];
	char HTTP_HOST[32];
	char SERVER_ADDR[100];
	char SERVER_PORT[10];
	char REMOTE_ADDR[100];
	char REMOTE_PORT[10];
	char FILENAME[30] = "./";
};

class server
{
public:
	server(boost::asio::io_context& io_context, short port)
		: acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
	{
		do_accept();
	}

private:
	void do_accept()
	{
		acceptor_.async_accept(
			[this](boost::system::error_code ec, tcp::socket socket)
			{
			if (!ec)
			{
				std::make_shared<session>(std::move(socket))->start();
			}

			do_accept();
			});
	}

	tcp::acceptor acceptor_;
};

int main(int argc, char* argv[])
{
	try
	{
		if (argc != 2)
		{
			std::cerr << "Usage: http_server <port>\n";
			return 1;
		}
		signal(SIGCHLD, SIG_IGN);
		boost::asio::io_context io_context;

		server s(io_context, std::atoi(argv[1]));

		io_context.run();
	}
	catch (std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << "\n";
	}

return 0;
}