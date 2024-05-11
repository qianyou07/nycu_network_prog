#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <unistd.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <fstream>
#include <vector>

#define MAX_SERVER_NUM 5

using boost::asio::ip::tcp;
using namespace std;

boost::asio::io_context io_context;

class client
    : public enable_shared_from_this<client>
{
public:
	client(unsigned int i, tcp::resolver::query q, string &input_file, shared_ptr<tcp::socket> web)
		: resolver_(io_context), shell_socket_(io_context), web_socket_(web), query_(move(q))
	{
		id = "s" + to_string(i+1);
		file.open("test_case/" + input_file, ios::in);
	}
	void start(){
		do_resolve();
	}

private:
	void do_write(size_t length){
		auto self(shared_from_this());
		boost::asio::async_write(shell_socket_, boost::asio::buffer(toShellMsg, length),
			[this, self](boost::system::error_code ec, std::size_t /*length*/)
			{
				if (!ec)
				{
				}
			});
	}
	void do_print(size_t length){
		string fromShellMsg(data_, data_+length);
		output_shell(id ,fromShellMsg);
		if(fromShellMsg.find("% ") != string::npos)
		{
			getline(file, toShellMsg);
			toShellMsg += "\n";
			output_command(id, toShellMsg);
			do_write(toShellMsg.size());
		}
	}
	void do_read(){
		auto self(shared_from_this());
		shell_socket_.async_receive(boost::asio::buffer(data_, max_length),
			[this, self](boost::system::error_code ec, size_t length)
			{
				if(!ec)
					do_print(length);
				do_read();
			});
	}
	void do_connect(tcp::resolver::iterator it){
		auto self(shared_from_this());
		shell_socket_.async_connect(*it,
			[this, self](boost::system::error_code ec)
			{
				if(!ec){
					do_read();
				}
			});
	}
	void do_resolve()
	{
		auto self(shared_from_this());
		resolver_.async_resolve(query_, 
			[this, self](boost::system::error_code ec, tcp::resolver::iterator it)
			{
				if (!ec)
				{
					do_connect(it);
				}
			});
	}
	void escape(std::string &data)
	{
		using boost::algorithm::replace_all;
		replace_all(data, "&",  "&amp;");
		replace_all(data, "\"", "&quot;");
		replace_all(data, "\'", "&apos;");
		replace_all(data, "<",  "&lt;");
		replace_all(data, ">",  "&gt;");
		replace_all(data, "\r\n", "&NewLine;");
		replace_all(data, "\n", "&NewLine;");
	}
	void output_shell(string &id, string content){
		escape(content);
		content = "<script>document.getElementById(\'" + id + "\').innerHTML += \'" + content + "\';</script>\n";
		do_write_web(content);
	}

	void output_command(string &id, string content){
		escape(content);
		content = "<script>document.getElementById(\'" + id + "\').innerHTML += \'<b>" + content + "</b>\';</script>\n";
		do_write_web(content);
	}
	void do_write_web(string &content){
		auto self(shared_from_this());
		boost::asio::async_write(*web_socket_, boost::asio::buffer(content.c_str(), content.size()),
			[this, self](boost::system::error_code ec, std::size_t /*length*/)
			{
				if (!ec)
				{
				}
			});
	}

	tcp::resolver resolver_;
	tcp::socket shell_socket_;
	shared_ptr<tcp::socket> web_socket_;
	tcp::resolver::query query_;
	string id;
	fstream file;
	string toShellMsg;
	enum { max_length = 1024 };
	char data_[max_length];
};

class session
: public std::enable_shared_from_this<session>
{
public:
	session(tcp::socket socket)
		: socket_(move(socket)), numOfQueries(0)
	{
		panel_data_ = 
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: text/html\r\n\r\n" 
			"<!DOCTYPE html>\n"
			"<html lang=\"en\">\n"
			"  <head>\n"
			"    <title>NP Project 3 Panel</title>\n"
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
			"      href=\"https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png\"\n"
			"    />\n"
			"    <style>\n"
			"      * {\n"
			"        font-family: 'Source Code Pro', monospace;\n"
			"      }\n"
			"    </style>\n"
			"  </head>\n"
			"  <body class=\"bg-secondary pt-5\">\n"
			"    <form action=\"console.cgi\" method=\"GET\">\n"
			"      <table class=\"table mx-auto bg-light\" style=\"width: inherit\">\n"
			"        <thead class=\"thead-dark\">\n"
			"          <tr>\n"
			"            <th scope=\"col\">#</th>\n"
			"            <th scope=\"col\">Host</th>\n"
			"            <th scope=\"col\">Port</th>\n"
			"            <th scope=\"col\">Input File</th>\n"
			"          </tr>\n"
			"        </thead>\n"
			"        <tbody>\n"
			"          <tr>\n"
			"            <th scope=\"row\" class=\"align-middle\">Session 1</th>\n"
			"            <td>\n"
			"              <div class=\"input-group\">\n"
			"                <select name=\"h0\" class=\"custom-select\">\n"
			"                  <option></option><option value=\"nplinux1.cs.nctu.edu.tw\">nplinux1</option><option value=\"nplinux2.cs.nctu.edu.tw\">nplinux2</option><option value=\"nplinux3.cs.nctu.edu.tw\">nplinux3</option><option value=\"nplinux4.cs.nctu.edu.tw\">nplinux4</option><option value=\"nplinux5.cs.nctu.edu.tw\">nplinux5</option><option value=\"nplinux6.cs.nctu.edu.tw\">nplinux6</option><option value=\"nplinux7.cs.nctu.edu.tw\">nplinux7</option><option value=\"nplinux8.cs.nctu.edu.tw\">nplinux8</option><option value=\"nplinux9.cs.nctu.edu.tw\">nplinux9</option><option value=\"nplinux10.cs.nctu.edu.tw\">nplinux10</option><option value=\"nplinux11.cs.nctu.edu.tw\">nplinux11</option><option value=\"nplinux12.cs.nctu.edu.tw\">nplinux12</option>\n"
			"                </select>\n"
			"                <div class=\"input-group-append\">\n"
			"                  <span class=\"input-group-text\">.cs.nctu.edu.tw</span>\n"
			"                </div>\n"
			"              </div>\n"
			"            </td>\n"
			"            <td>\n"
			"              <input name=\"p0\" type=\"text\" class=\"form-control\" size=\"5\" />\n"
			"            </td>\n"
			"            <td>\n"
			"              <select name=\"f0\" class=\"custom-select\">\n"
			"                <option></option>\n"
			"                <option value=\"t1.txt\">t1.txt</option><option value=\"t2.txt\">t2.txt</option><option value=\"t3.txt\">t3.txt</option><option value=\"t4.txt\">t4.txt</option><option value=\"t5.txt\">t5.txt</option>\n"
			"              </select>\n"
			"            </td>\n"
			"          </tr>\n"
			"          <tr>\n"
			"            <th scope=\"row\" class=\"align-middle\">Session 2</th>\n"
			"            <td>\n"
			"              <div class=\"input-group\">\n"
			"                <select name=\"h1\" class=\"custom-select\">\n"
			"                  <option></option><option value=\"nplinux1.cs.nctu.edu.tw\">nplinux1</option><option value=\"nplinux2.cs.nctu.edu.tw\">nplinux2</option><option value=\"nplinux3.cs.nctu.edu.tw\">nplinux3</option><option value=\"nplinux4.cs.nctu.edu.tw\">nplinux4</option><option value=\"nplinux5.cs.nctu.edu.tw\">nplinux5</option><option value=\"nplinux6.cs.nctu.edu.tw\">nplinux6</option><option value=\"nplinux7.cs.nctu.edu.tw\">nplinux7</option><option value=\"nplinux8.cs.nctu.edu.tw\">nplinux8</option><option value=\"nplinux9.cs.nctu.edu.tw\">nplinux9</option><option value=\"nplinux10.cs.nctu.edu.tw\">nplinux10</option><option value=\"nplinux11.cs.nctu.edu.tw\">nplinux11</option><option value=\"nplinux12.cs.nctu.edu.tw\">nplinux12</option>\n"
			"                </select>\n"
			"                <div class=\"input-group-append\">\n"
			"                  <span class=\"input-group-text\">.cs.nctu.edu.tw</span>\n"
			"                </div>\n"
			"              </div>\n"
			"            </td>\n"
			"            <td>\n"
			"              <input name=\"p1\" type=\"text\" class=\"form-control\" size=\"5\" />\n"
			"            </td>\n"
			"            <td>\n"
			"              <select name=\"f1\" class=\"custom-select\">\n"
			"                <option></option>\n"
			"                <option value=\"t1.txt\">t1.txt</option><option value=\"t2.txt\">t2.txt</option><option value=\"t3.txt\">t3.txt</option><option value=\"t4.txt\">t4.txt</option><option value=\"t5.txt\">t5.txt</option>\n"
			"              </select>\n"
			"            </td>\n"
			"          </tr>\n"
			"          <tr>\n"
			"            <th scope=\"row\" class=\"align-middle\">Session 3</th>\n"
			"            <td>\n"
			"              <div class=\"input-group\">\n"
			"                <select name=\"h2\" class=\"custom-select\">\n"
			"                  <option></option><option value=\"nplinux1.cs.nctu.edu.tw\">nplinux1</option><option value=\"nplinux2.cs.nctu.edu.tw\">nplinux2</option><option value=\"nplinux3.cs.nctu.edu.tw\">nplinux3</option><option value=\"nplinux4.cs.nctu.edu.tw\">nplinux4</option><option value=\"nplinux5.cs.nctu.edu.tw\">nplinux5</option><option value=\"nplinux6.cs.nctu.edu.tw\">nplinux6</option><option value=\"nplinux7.cs.nctu.edu.tw\">nplinux7</option><option value=\"nplinux8.cs.nctu.edu.tw\">nplinux8</option><option value=\"nplinux9.cs.nctu.edu.tw\">nplinux9</option><option value=\"nplinux10.cs.nctu.edu.tw\">nplinux10</option><option value=\"nplinux11.cs.nctu.edu.tw\">nplinux11</option><option value=\"nplinux12.cs.nctu.edu.tw\">nplinux12</option>\n"
			"                </select>\n"
			"                <div class=\"input-group-append\">\n"
			"                  <span class=\"input-group-text\">.cs.nctu.edu.tw</span>\n"
			"                </div>\n"
			"              </div>\n"
			"            </td>\n"
			"            <td>\n"
			"              <input name=\"p2\" type=\"text\" class=\"form-control\" size=\"5\" />\n"
			"            </td>\n"
			"            <td>\n"
			"              <select name=\"f2\" class=\"custom-select\">\n"
			"                <option></option>\n"
			"                <option value=\"t1.txt\">t1.txt</option><option value=\"t2.txt\">t2.txt</option><option value=\"t3.txt\">t3.txt</option><option value=\"t4.txt\">t4.txt</option><option value=\"t5.txt\">t5.txt</option>\n"
			"              </select>\n"
			"            </td>\n"
			"          </tr>\n"
			"          <tr>\n"
			"            <th scope=\"row\" class=\"align-middle\">Session 4</th>\n"
			"            <td>\n"
			"              <div class=\"input-group\">\n"
			"                <select name=\"h3\" class=\"custom-select\">\n"
			"                  <option></option><option value=\"nplinux1.cs.nctu.edu.tw\">nplinux1</option><option value=\"nplinux2.cs.nctu.edu.tw\">nplinux2</option><option value=\"nplinux3.cs.nctu.edu.tw\">nplinux3</option><option value=\"nplinux4.cs.nctu.edu.tw\">nplinux4</option><option value=\"nplinux5.cs.nctu.edu.tw\">nplinux5</option><option value=\"nplinux6.cs.nctu.edu.tw\">nplinux6</option><option value=\"nplinux7.cs.nctu.edu.tw\">nplinux7</option><option value=\"nplinux8.cs.nctu.edu.tw\">nplinux8</option><option value=\"nplinux9.cs.nctu.edu.tw\">nplinux9</option><option value=\"nplinux10.cs.nctu.edu.tw\">nplinux10</option><option value=\"nplinux11.cs.nctu.edu.tw\">nplinux11</option><option value=\"nplinux12.cs.nctu.edu.tw\">nplinux12</option>\n"
			"                </select>\n"
			"                <div class=\"input-group-append\">\n"
			"                  <span class=\"input-group-text\">.cs.nctu.edu.tw</span>\n"
			"                </div>\n"
			"              </div>\n"
			"            </td>\n"
			"            <td>\n"
			"              <input name=\"p3\" type=\"text\" class=\"form-control\" size=\"5\" />\n"
			"            </td>\n"
			"            <td>\n"
			"              <select name=\"f3\" class=\"custom-select\">\n"
			"                <option></option>\n"
			"                <option value=\"t1.txt\">t1.txt</option><option value=\"t2.txt\">t2.txt</option><option value=\"t3.txt\">t3.txt</option><option value=\"t4.txt\">t4.txt</option><option value=\"t5.txt\">t5.txt</option>\n"
			"              </select>\n"
			"            </td>\n"
			"          </tr>\n"
			"          <tr>\n"
			"            <th scope=\"row\" class=\"align-middle\">Session 5</th>\n"
			"            <td>\n"
			"              <div class=\"input-group\">\n"
			"                <select name=\"h4\" class=\"custom-select\">\n"
			"                  <option></option><option value=\"nplinux1.cs.nctu.edu.tw\">nplinux1</option><option value=\"nplinux2.cs.nctu.edu.tw\">nplinux2</option><option value=\"nplinux3.cs.nctu.edu.tw\">nplinux3</option><option value=\"nplinux4.cs.nctu.edu.tw\">nplinux4</option><option value=\"nplinux5.cs.nctu.edu.tw\">nplinux5</option><option value=\"nplinux6.cs.nctu.edu.tw\">nplinux6</option><option value=\"nplinux7.cs.nctu.edu.tw\">nplinux7</option><option value=\"nplinux8.cs.nctu.edu.tw\">nplinux8</option><option value=\"nplinux9.cs.nctu.edu.tw\">nplinux9</option><option value=\"nplinux10.cs.nctu.edu.tw\">nplinux10</option><option value=\"nplinux11.cs.nctu.edu.tw\">nplinux11</option><option value=\"nplinux12.cs.nctu.edu.tw\">nplinux12</option>\n"
			"                </select>\n"
			"                <div class=\"input-group-append\">\n"
			"                  <span class=\"input-group-text\">.cs.nctu.edu.tw</span>\n"
			"                </div>\n"
			"              </div>\n"
			"            </td>\n"
			"            <td>\n"
			"              <input name=\"p4\" type=\"text\" class=\"form-control\" size=\"5\" />\n"
			"            </td>\n"
			"            <td>\n"
			"              <select name=\"f4\" class=\"custom-select\">\n"
			"                <option></option>\n"
			"                <option value=\"t1.txt\">t1.txt</option><option value=\"t2.txt\">t2.txt</option><option value=\"t3.txt\">t3.txt</option><option value=\"t4.txt\">t4.txt</option><option value=\"t5.txt\">t5.txt</option>\n"
			"              </select>\n"
			"            </td>\n"
			"          </tr>\n"
			"          <tr>\n"
			"            <td colspan=\"3\"></td>\n"
			"            <td>\n"
			"              <button type=\"submit\" class=\"btn btn-info btn-block\">Run</button>\n"
			"            </td>\n"
			"          </tr>\n"
			"        </tbody>\n"
			"      </table>\n"
			"    </form>\n"
			"  </body>\n"
			"</html>\n";
	}

	void start()
	{
		do_read();
	}

	void do_write(string &data){
		auto self(shared_from_this());
		boost::asio::async_write(socket_, boost::asio::buffer(data.c_str(), data.size()),
			[this, self](boost::system::error_code ec, std::size_t /*length*/)
			{
				if (!ec)
				{
				}
			});
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

	void do_request()
	{
		if(strcmp(FILENAME, "./panel.cgi") == 0){
			cout<<"do panel\n";
			do_write(panel_data_);
		}
		else if(strcmp(FILENAME, "./console.cgi") == 0){
			cout<<"do console\n";
			parseQueryString();
			printHeader();
			do_write(console_data_);
			shared_ptr<tcp::socket> shared_socket(make_shared<tcp::socket>(move(socket_)));
			for(unsigned int i=0;i<numOfQueries;i++){
				make_shared<client>(i, tcp::resolver::query(queryInfos[i].host, queryInfos[i].port), queryInfos[i].input_file, shared_socket)->start();
			}
		}
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

	void parseQueryString(){
		string QUERY_STRING_(QUERY_STRING);
		vector<string> stringVector;
		boost::split( stringVector, QUERY_STRING_, boost::is_any_of( "&" ), boost::token_compress_on );
		for(auto it = stringVector.begin(); it != stringVector.end(); it++ ){
			vector<string> half;
			boost::split( half, *it, boost::is_any_of( "=" ), boost::token_compress_on );
			if(!half[1].empty()){
				switch(half[0][0]){
					case 'h':
						queryInfos[numOfQueries].host = (*it).substr((*it).find('=')+1);
						break;
					case 'p':
						queryInfos[numOfQueries].port = (*it).substr((*it).find('=')+1);
						break;
					case 'f':
						queryInfos[numOfQueries].input_file = (*it).substr((*it).find('=')+1);
						queryInfos[numOfQueries].id = "s" + to_string(numOfQueries+1);
						numOfQueries++;
				}
			}
		}
	}

	void printHeader(){
		string th = "", td = "";
		for(unsigned int i=0;i<numOfQueries;i++){
			th += "          <th scope=\"col\">" + queryInfos[i].host + ":" + queryInfos[i].port + "</th>\n";
			td += "          <td><pre id=\"" + queryInfos[i].id + "\" class=\"mb-0\"></pre></td>\n";
		}
		console_data_ = 
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: text/html\r\n\r\n" 
			"<!DOCTYPE html>\n"
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
			"        <tr>\n"
					+ th +
			"       </tr>\n"
			"      </thead>\n"
			"      <tbody>\n"
			"        <tr>\n"
					+ td +
			"        </tr>\n"
			"      </tbody>\n"
			"    </table>\n"
			"  </body>\n"
			"</html>\n";
	}

	tcp::socket socket_;
	enum { max_length = 1024 };
	char data_[max_length];
	string panel_data_;
	string console_data_;
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
	

	struct queryInfo{
		string host;
		string port;
		string input_file;
		string id;
	}queryInfos[MAX_SERVER_NUM];

	unsigned int numOfQueries;
};

class server
{
public:
	server(short port)
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

		server s(std::atoi(argv[1]));

		io_context.run();
	}
	catch (std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << "\n";
	}

return 0;
}