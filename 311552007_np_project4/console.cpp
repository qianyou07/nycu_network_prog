#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string>
#include <vector>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <fstream>

#define MAX_SERVER_NUM 5

using boost::asio::ip::tcp;
using namespace std;

struct queryInfo
{
	string host;
	string port;
	string input_file;
	string id;
} queryInfos[MAX_SERVER_NUM];

string sockHost, sockPort;

unsigned int numOfQueries = 0;

void parseQueryString()
{
	string QUERY_STRING = getenv("QUERY_STRING");
	vector<string> stringVector;
	boost::split(stringVector, QUERY_STRING, boost::is_any_of("&"), boost::token_compress_on);
	for (auto it = stringVector.begin(); it != stringVector.end(); it++)
	{
		vector<string> half;
		boost::split(half, *it, boost::is_any_of("="), boost::token_compress_on);
		if (!half[1].empty())
		{
			switch (half[0][0])
			{
			case 'h':
				queryInfos[numOfQueries].host = (*it).substr((*it).find('=') + 1);
				break;
			case 'p':
				queryInfos[numOfQueries].port = (*it).substr((*it).find('=') + 1);
				break;
			case 'f':
				queryInfos[numOfQueries].input_file = (*it).substr((*it).find('=') + 1);
				queryInfos[numOfQueries].id = "s" + to_string(numOfQueries + 1);
				numOfQueries++;
				break;
			case 's':
				if (half[0][1] == 'h')
				{
					sockHost = (*it).substr((*it).find('=') + 1);
				}
				else if (half[0][1] == 'p')
				{
					sockPort = (*it).substr((*it).find('=') + 1);
				}
			}
		}
	}
}

void printHeader()
{
	string th = "", td = "";
	for (unsigned int i = 0; i < numOfQueries; i++)
	{
		th += "          <th scope=\"col\">" + queryInfos[i].host + ":" + queryInfos[i].port + "</th>\n";
		td += "          <td><pre id=\"" + queryInfos[i].id + "\" class=\"mb-0\"></pre></td>\n";
	}
	cout << "<!DOCTYPE html>\n"
			"<html lang=\"en\">\n"
			"  <head>\n"
			"    <meta charset=\"UTF-8\" />\n"
			"    <title>NP Project 3 Sample Console</title>\n"
			"    <link\n"
			"      rel=\"stylesheet\"\n"
			"      href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\n"
			"      integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\n"
			"      crossorigin=\"anonymous\"\n"
			"    />\n"
			"    <link\n"
			"      href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\n"
			"      rel=\"stylesheet\"\n"
			"    />\n"
			"    <link\n"
			"      rel=\"icon\"\n"
			"      type=\"image/png\"\n"
			"      href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"\n"
			"    />\n"
			"    <style>\n"
			"      * {\n"
			"        font-family: 'Source Code Pro', monospace;\n"
			"        font-size: 1rem !important;\n"
			"      }\n"
			"      body {\n"
			"        background-color: #212529;\n"
			"      }\n"
			"      pre {\n"
			"        color: #cccccc;\n"
			"      }\n"
			"      b {\n"
			"        color: #01b468;\n"
			"      }\n"
			"    </style>\n"
			"  </head>\n"
			"  <body>\n"
			"    <table class=\"table table-dark table-bordered\">\n"
			"      <thead>\n"
			"        <tr>\n" +
				th +
				"       </tr>\n"
				"      </thead>\n"
				"      <tbody>\n"
				"        <tr>\n" +
				td +
				"        </tr>\n"
				"      </tbody>\n"
				"    </table>\n"
				"  </body>\n"
				"</html>\n";
	cout.flush();
}

void escape(std::string &data)
{
	using boost::algorithm::replace_all;
	replace_all(data, "&", "&amp;");
	replace_all(data, "\"", "&quot;");
	replace_all(data, "\'", "&apos;");
	replace_all(data, "<", "&lt;");
	replace_all(data, ">", "&gt;");
	replace_all(data, "\r\n", "&NewLine;");
	replace_all(data, "\n", "&NewLine;");
}

void output_shell(string &id, string &content)
{
	escape(content);
	cout << "<script>document.getElementById(\'" + id + "\').innerHTML += \'" + content + "\';</script>\n";
	cout.flush();
}

void output_command(string &id, string &content)
{
	escape(content);
	cout << "<script>document.getElementById(\'" + id + "\').innerHTML += \'<b>" + content + "</b>\';</script>\n";
	cout.flush();
}

void printQuery()
{
	for (unsigned int i = 0; i < numOfQueries; i++)
	{
		cout << queryInfos[i].host << endl;
		cout << queryInfos[i].port << endl;
		cout << queryInfos[i].input_file << endl;
	}
}

class client
	: public enable_shared_from_this<client>
{
public:
	client(boost::asio::io_context &io_context, unsigned int i, tcp::resolver::query q, tcp::resolver::query sock_q)
		: resolver_(io_context), socket_(io_context), query_(move(q)), sock_query_(move(sock_q))
	{
		id = "s" + to_string(i + 1);
		file.open("test_case/" + queryInfos[i].input_file, ios::in);
	}
	void start()
	{
		do_resolve();
	}

private:
	void do_write(size_t length)
	{
		auto self(shared_from_this());
		boost::asio::async_write(socket_, boost::asio::buffer(toShellMsg, length),
								 [this, self](boost::system::error_code ec, std::size_t /*length*/)
								 {
									 if (!ec)
									 {
										 output_command(id, toShellMsg);
									 }
								 });
	}
	void do_print(size_t length)
	{
		string fromShellMsg(data_, data_ + length);
		output_shell(id, fromShellMsg);
		if (fromShellMsg.find("% ") != string::npos)
		{
			// toShellMsg = "";
			getline(file, toShellMsg);
			toShellMsg += "\n";
			do_write(toShellMsg.size());
		}
	}
	void do_read()
	{
		auto self(shared_from_this());
		socket_.async_receive(boost::asio::buffer(data_, max_length),
							  [this, self](boost::system::error_code ec, size_t length)
							  {
								  if (!ec)
									  do_print(length);
								  do_read();
							  });
	}
	void do_recv_reply()
	{
		auto self(shared_from_this());
		socket_.async_receive(boost::asio::buffer(reply_buffer, 8),
							  [this, self](boost::system::error_code ec, size_t length)
							  {
								  if (!ec)
								  {
									  if (reply_buffer[1] == 90U)
										  do_read();
									  else
									  {
										  exit(0);
									  }
								  }
							  });
	}
	void do_request(unsigned long addr, unsigned short port)
	{
		request_buffer[0] = 4U;
		request_buffer[1] = 1U;
		request_buffer[2] = port >> 8;
		request_buffer[3] = port;
		request_buffer[4] = addr >> 24;
		request_buffer[5] = addr >> 16;
		request_buffer[6] = addr >> 8;
		request_buffer[7] = addr;
		request_buffer[8] = 0U;
		auto self(shared_from_this());
		boost::asio::async_write(socket_, boost::asio::buffer(request_buffer, 9),
								 [this, self](boost::system::error_code ec, std::size_t /*length*/)
								 {
									 if (!ec)
									 {
										 do_recv_reply();
									 }
								 });
	}
	void do_connect(tcp::resolver::iterator it)
	{
		auto self(shared_from_this());
		socket_.async_connect(*it,
							  [this, self](boost::system::error_code ec)
							  {
								  if (!ec)
								  {
									  auto it = resolver_.resolve(query_);
									  do_request(it->endpoint().address().to_v4().to_ulong(), it->endpoint().port());
								  }
							  });
	}
	void do_resolve()
	{
		auto self(shared_from_this());
		resolver_.async_resolve(sock_query_,
								[this, self](boost::system::error_code ec, tcp::resolver::iterator it)
								{
									if (!ec)
									{
										do_connect(it);
									}
								});
	}

	tcp::resolver resolver_;
	tcp::socket socket_;
	tcp::resolver::query query_, sock_query_;
	string id;
	fstream file;
	string toShellMsg;
	unsigned char request_buffer[9], reply_buffer[1024];
	enum
	{
		max_length = 1024
	};
	char data_[max_length];
};

int main(int argc, char *argv[])
{
	try
	{
		cout << "Content-type: text/html\r\n\r\n";
		parseQueryString();
		printHeader();
		boost::asio::io_context io_context;
		for (unsigned int i = 0; i < numOfQueries; i++)
		{
			make_shared<client>(io_context, i, tcp::resolver::query(queryInfos[i].host, queryInfos[i].port), tcp::resolver::query(sockHost, sockPort))->start();
		}

		io_context.run();
	}
	catch (std::exception &e)
	{
		std::cerr << "Exception: " << e.what() << "\n";
	}

	return 0;
}