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
#include <fstream>
#include <regex>

using boost::asio::ip::tcp;
using namespace std;

boost::asio::io_context io_context;

std::string ReplaceAll(std::string str, const std::string &from, const std::string &to)
{
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos)
	{
		str.replace(start_pos, from.length(), to);
		start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
	}
	return str;
}

class session
	: public std::enable_shared_from_this<session>
{
public:
	session(tcp::socket socket)
		: server_socket_(io_context), client_socket_(std::move(socket)), resolver_(io_context), acceptor_(io_context)
	{
	}

	void start()
	{
		do_start();
	}

private:
	void do_start()
	{
		auto self(shared_from_this());
		client_socket_.async_read_some(boost::asio::buffer(data_, max_length),
									   [this, self](boost::system::error_code ec, size_t length)
									   {
										   if (!ec)
										   {
											   ParseRequest(length);
											   // printInfo();
											   // do_request();
										   }
									   });
	}

	void ParseRequest(size_t length)
	{
		VN = data_[0];
		CD = data_[1];
		DSTPORT = data_[2] << 8 | data_[3];
		DSTIP = data_[4] << 24 | data_[5] << 16 | data_[6] << 8 | data_[7];
		// DSTIP is 0.0.0.x
		if (DSTIP < 256)
		{
			int start = 8, end = length - 1;
			while (data_[start++] != 0);
			for (int i = start; i < end; i++)
			{
				DOMAIN_NAME += data_[i];
			}
			do_resolve(tcp::resolver::query(DOMAIN_NAME, to_string(DSTPORT)));
		}
		else
		{
			do_resolve(tcp::resolver::query(tcp::resolver::query(boost::asio::ip::address_v4(DSTIP).to_string(), to_string(DSTPORT))));
		}
	}

	void do_resolve(tcp::resolver::query query_)
	{
		auto self(shared_from_this());
		resolver_.async_resolve(query_,
								[this, self](boost::system::error_code ec, tcp::resolver::iterator it)
								{
									if (!ec)
									{
										tcp::resolver::iterator end;
										for(;it!=end;it++){
											if(it->endpoint().address().is_v4())
												break;
										}
										DSTPORT = it->endpoint().port();
										DSTIP = it->endpoint().address().to_v4().to_ulong();
										if (pass_firewall(it))
										{
											if (CD == 1U) // connect
												do_connect(it);
											else if (CD == 2U)
											{ // bind
												do_bind();
												do_reply(0U, 90U, acceptor_.local_endpoint().port(), 0UL);
												do_accept();
											}
										}
										else
										{
											printInfo(false);
											do_reply(0U, 91U, DSTPORT, DSTIP);
										}
									}
								});
	}

	bool pass_firewall(tcp::resolver::iterator it)
	{
		ifstream in;
		in.open("socks.conf");
		string filler, TYPE, ADDR;
		while (in >> filler >> TYPE >> ADDR)
		{
			ADDR = ReplaceAll(ADDR, "*", "\\d{1,3}");
			ADDR = ReplaceAll(ADDR, ".", "\\.");
			regex rule(ADDR);
			if (CD == 1U && TYPE == "c" && regex_match(it->endpoint().address().to_string(), rule))
			{
				// connect
				in.close();
				return true;
			}
			else if (CD == 2U && TYPE == "b" && regex_match(it->endpoint().address().to_string(), rule))
			{
				// bind
				in.close();
				return true;
			}
		}

		in.close();
		return false;
	}

	void do_connect(tcp::resolver::iterator it)
	{
		auto self(shared_from_this());
		server_socket_.async_connect(*it,
									 [this, self](boost::system::error_code ec)
									 {
										 if (!ec)
										 {
											 printInfo(true);
											 do_reply(0U, 90U, DSTPORT, DSTIP);
											 do_read_server();
											 do_read_client();
										 }
										 else
										 {
											 printInfo(false);
											 do_reply(0U, 91U, DSTPORT, DSTIP);
											 cout << "connect fail\n";
										 }
									 });
	}

	void do_bind()
	{
		tcp::endpoint endpoint_(tcp::v4(), 0);
		acceptor_.open(endpoint_.protocol());
		acceptor_.set_option(tcp::acceptor::reuse_address(true));
		acceptor_.bind(endpoint_);
		acceptor_.listen();
	}

	void do_reply(unsigned int VN, unsigned int CD, unsigned short DSTPORT, unsigned long DSTIP)
	{
		replyMsg[0] = VN;
		replyMsg[1] = CD;
		replyMsg[2] = DSTPORT >> 8;
		replyMsg[3] = DSTPORT;
		replyMsg[4] = DSTIP >> 24;
		replyMsg[5] = DSTIP >> 16;
		replyMsg[6] = DSTIP >> 8;
		replyMsg[7] = DSTIP;
		auto self(shared_from_this());
		boost::asio::async_write(client_socket_, boost::asio::buffer(replyMsg, 8),
								 [this, self](boost::system::error_code ec, std::size_t /*length*/) {
								 });
	}

	void do_accept()
	{
		auto self(shared_from_this());
		acceptor_.async_accept(server_socket_,
							   [this, self](boost::system::error_code ec)
							   {
								   if (!ec)
								   {
									   printInfo(true);
									   do_reply(0U, 90U, acceptor_.local_endpoint().port(), 0UL);
									   acceptor_.close();
									   do_read_server();
									   do_read_client();
								   }
								   else
								   {
									   printInfo(false);
									   do_reply(0U, 91U, acceptor_.local_endpoint().port(), 0UL);
									   acceptor_.close();
								   }
							   });
	}

	void do_read_server()
	{
		auto self(shared_from_this());
		server_socket_.async_receive(boost::asio::buffer(server_buffer, max_length),
									 [this, self](boost::system::error_code ec, size_t length)
									 {
										 if (!ec || ec == boost::asio::error::eof)
											 do_write_client(server_buffer, length);
										 else
										 {
											 client_socket_.close();
											 exit(0);
											 //  throw system_error{ec};
										 }
									 });
	}

	void do_write_client(unsigned char *data, size_t length)
	{
		auto self(shared_from_this());
		boost::asio::async_write(client_socket_, boost::asio::buffer(data, length),
								 [this, self](boost::system::error_code ec, std::size_t /*length*/)
								 {
									 if (!ec || ec == boost::asio::error::eof)
										 do_read_server();
									 else
									 {
										 server_socket_.close();
										 exit(0);
										 //  throw system_error{ec};
									 }
								 });
	}

	void do_read_client()
	{
		auto self(shared_from_this());
		client_socket_.async_receive(boost::asio::buffer(client_buffer, max_length),
									 [this, self](boost::system::error_code ec, size_t length)
									 {
										 if (!ec || ec == boost::asio::error::eof)
											 do_write_server(client_buffer, length);
										 else
										 {
											 client_socket_.close();
											 exit(0);
											 //  throw system_error{ec};
										 }
									 });
	}

	void do_write_server(unsigned char *data, size_t length)
	{
		auto self(shared_from_this());
		boost::asio::async_write(server_socket_, boost::asio::buffer(data, length),
								 [this, self](boost::system::error_code ec, size_t length)
								 {
									 if (!ec)
										 do_read_client();
									 else
									 {
										 server_socket_.close();
										 exit(0);
										 //  throw system_error{ec};
									 }
								 });
	}

	void printInfo(bool is_accept)
	{
		cout << "<S_IP>: " << client_socket_.remote_endpoint().address().to_string() << endl;
		cout << "<S_PORT>: " << client_socket_.remote_endpoint().port() << endl;
		cout << "<D_IP>: " << boost::asio::ip::address_v4(DSTIP).to_string() << endl;
		cout << "<D_PORT>: " << to_string(DSTPORT) << endl;
		if (CD == 1)
			cout << "<Command>: CONNECT\n";
		else if (CD == 2)
			cout << "<Command>: BIND\n";
		if (is_accept)
			cout << "<Reply>: Accept\n";
		else
			cout << "<Reply>: Reject\n";
	}

	tcp::socket server_socket_, client_socket_;
	tcp::resolver resolver_;
	tcp::acceptor acceptor_;
	enum
	{
		max_length = 65535
	};
	unsigned char data_[max_length], server_buffer[max_length], client_buffer[max_length];
	unsigned char replyMsg[8];
	unsigned int VN;
	unsigned int CD;
	unsigned short DSTPORT;
	unsigned long DSTIP;
	string DOMAIN_NAME;
};

class server
{
public:
	server(boost::asio::io_context &io_context, short port)
		: acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
	{
		acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
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
					io_context.notify_fork(boost::asio::io_context::fork_prepare);
					pid_t pid;
					while ((pid = fork()) < 0)
					{
						cout << "fork error\n";
						int status = 0;
						waitpid(-1, &status, 0);
					}
					if (pid == 0)
					{
						io_context.notify_fork(boost::asio::io_context::fork_child);
						acceptor_.close();
						std::make_shared<session>(std::move(socket))->start();
					}
					else if (pid > 0)
					{
						io_context.notify_fork(boost::asio::io_context::fork_parent);
						socket.close();
					}
				}

				do_accept();
			});
	}

	tcp::acceptor acceptor_;
};

int main(int argc, char *argv[])
{
	try
	{
		if (argc != 2)
		{
			std::cerr << "Usage: http_server <port>\n";
			return 1;
		}
		signal(SIGCHLD, SIG_IGN);

		server s(io_context, std::atoi(argv[1]));

		io_context.run();
	}
	catch (std::exception &e)
	{
		// std::cerr << "Exception: " << e.what() << "\n";
	}

	return 0;
}