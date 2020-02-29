#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/regex.hpp>
#include <map>
#define ISPROMPT 1
using namespace std;
using namespace boost::asio;

io_service global_io_service;

void printHTMLStructure(map<string, string> queryMap) {
	cout << "<!DOCTYPE html>\r\n"
	     << "<html lang='en'>\r\n"
	     << "  <head>\r\n"
	     << "    <meta charset='UTF-8' />\r\n"
	     << "    <title>NP Project 3 Console</title>\r\n"
	     << "    <link\r\n"
	     << "      rel='stylesheet'\r\n"
	     << "      href='https://stackpath.bootstrapcdn.com/bootstrap/4.1.3/css/bootstrap.min.css'\r\n"
	     << "      integrity='sha384-MCw98/SFnGE8fJT3GXwEOngsV7Zt27NXFoaoApmYm81iuXoPkFOJwJ8ERdknLPMO'\r\n"
	     << "      crossorigin='anonymous'\r\n"
	     << "    />\r\n"
	     << "    <link\r\n"
	     << "      href='https://fonts.googleapis.com/css?family=Source+Code+Pro'\r\n"
	     << "      rel='stylesheet'\r\n"
	     << "    />\r\n"
	     << "    <link\r\n"
	     << "      rel='icon'\r\n"
	     << "      type='image/png'\r\n"
	     << "      href='https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png'\r\n"
	     << "    />\r\n"
	     << "    <style>\r\n"
	     << "      * {\r\n"
	     << "        font-family: 'Source Code Pro', monospace;\r\n"
	     << "        font-size: 1rem !important;\r\n"
	     << "      }\r\n"
	     << "      body {\r\n"
	     << "        background-color: #212529;\r\n"
	     << "      }\r\n"
	     << "      pre {\r\n"
	     << "        color: #cccccc;\r\n"
	     << "      }\r\n"
	     << "      b {\r\n"
	     << "        color: #ffffff;\r\n"
	     << "      }\r\n"
	     << "    </style>\r\n"
	     << "  </head>\r\n"
	     << "  <body>\r\n"
	     << "    <table class='table table-dark table-bordered'>\r\n"
	     << "      <thead>\r\n"
	     << "        <tr>\r\n" << flush;
	for (int i = 0; i < 5; i++) {
		if (queryMap["h" + to_string(i)] != "") {
			cout << "<th scope='col'>" << queryMap["h" + to_string(i)] << ":" << queryMap["p" + to_string(i)] << "</th>\r\n";
		}		
	}
	cout << "        </tr>\r\n"
	     << "      </thead>\r\n"
	     << "      <tbody>\r\n"
	     << "        <tr>\r\n" << flush;
	for (int i = 0, j = 0; i < 5; i++) {
		if (queryMap["h" + to_string(i)] != "") {
			cout << "<td><pre id='s" << j << "' class='mb-0'></pre></td>\r\n";
			j++;
		}
	}
	cout << "        </tr>\r\n"
	     << "      </tbody>\r\n"
	     << "    </table>\r\n"
	     << "  </body>\r\n"
	     << "</html>\r\n" << flush;
}

void printHTMLAdd(string recvData, int id, bool isPrompt) {
	string escapedData;
	for(auto&& ch : recvData) {
		if (int(ch) == int('%')) {
			escapedData += ("&#" + to_string(int('%')) + ";");
			escapedData += ("&#" + to_string(int(' ')) + ";");
			break;
		}
		escapedData += ("&#" + to_string(int(ch)) + ";");
	}
	for(int i = 0; i < escapedData.length(); i++) {
		switch(escapedData[i]) {
			case '\n': escapedData.replace(i, 1, "&NewLine;"); break;
			//case '&': recvData.replace(i, 1, "&amp;"); break;
			//case '\"': recvData.replace(i, 1, "&quot;"); break;
			//case '\'': recvData.replace(i, 1, "&apos;"); break;
			//case '<': recvData.replace(i, 1, "&lt;"); break;
			//case '>': recvData.replace(i, 1, "&gt;"); break;
			default: break;
		}
	}
	cout << "<script>document.getElementById('s" << id << "').innerHTML += '" << escapedData << "';</script>" << flush;
}

class ShellClient : public enable_shared_from_this<ShellClient> {
private:
	enum { max_length = 1024 };
	array<unsigned char, max_length> _data;
	ip::tcp::socket _socket;
	ip::tcp::resolver _resolver;
	ip::tcp::resolver::query _query;
	string _host;
	string _port;
	string _socks_host;
	string _socks_port;
	string _filename;
	fstream _file;
	int _id;

public:
	ShellClient(string socks_host, string socks_port, string host, string port, string file, int id) : _socks_host(socks_host),
	                                                                                                   _socks_port(socks_port),
	                                                                                                   _host(host),
	                                                                                                   _port(port),
	                                                                                                   _filename(file),
	                                                                                                   _socket(global_io_service),
	                                                                                                   _resolver(global_io_service),
	                                                                                                   _id(id),
	                                                                                                   _query(host, port) {
		_file.open("./test_case/" + _filename, fstream::in);
	}
	void start() {
		do_resolve();
	}
private:
	void do_resolve_proxy(string server_address) {
		auto self(shared_from_this());
		ip::tcp::resolver::query socks_query(_socks_host, _socks_port);
		cout << _socks_host << " " << _socks_port << endl;
		_resolver.async_resolve(socks_query, [this, self, server_address](const boost::system::error_code& ec, ip::tcp::resolver::iterator it) {
			if (!ec) {
				cout << "async_resolve success" << endl;
				do_connect_proxy(it, server_address);
			} else {
				cout << "error async_resolve" << endl;
			}
		});
	}
	void do_connect_proxy(ip::tcp::resolver::iterator it, string server_address) {
		auto self(shared_from_this());
		_socket.async_connect(*it, [this, self, server_address](const boost::system::error_code& ec) {
			if (!ec) {
				cout << "do_connect_proxy success" << endl;
				do_request(server_address);
			} else {
				cout << "error async_connect proxy " << ec.message() << endl;
			}
		});
	}
	void do_request(string server_address) {
		auto self(shared_from_this());
		vector<string> splited_addr;
		boost::split(splited_addr, server_address, boost::is_any_of("."), boost::token_compress_on);
		unsigned char request_data[9] = {4, 1, (unsigned char)(stoi(_port) >> 8), (unsigned char)(stoi(_port) & (unsigned short)255), (unsigned char)stoi(splited_addr[0]), (unsigned char)stoi(splited_addr[1]), (unsigned char)stoi(splited_addr[2]), (unsigned char)stoi(splited_addr[3]), 0};
		cout << (unsigned short)request_data[4] << endl;
		cout << (stoi(_port) >> 8) << " " << (stoi(_port) & (unsigned short)255) << endl;
		_socket.async_send(buffer(request_data, 9), [this, self](boost::system::error_code ec, size_t length) {
			if (!ec) {
				cout << "do_request success" << endl;
				do_wait_reply();
			} else {
				cout << "error do_request" << endl;
			}
		});
	}
	void do_wait_reply() {
		auto self(shared_from_this());
		_socket.async_read_some(buffer(_data, max_length), [this, self](boost::system::error_code ec, size_t length) {
			if (!ec) {
				if (_data.data()[1] == 90) {
					cout << "proxy connect success" << endl;
					_data.fill('\0');
					do_read();
				} else if (_data.data()[1] == 91) {
					cout << "reject" << endl;
				} else {
					cout << "error do_wait_reply" << endl;
				}
			} else {
				cout << "error do_wait_reply " << ec.message() << endl;
			}
		});
	}
	void do_resolve() {
		auto self(shared_from_this());

		_resolver.async_resolve(_query, [this, self](const boost::system::error_code& ec, ip::tcp::resolver::iterator it) {
			if (!ec) {
				cout << "async_resolve success" << endl;
				if (_socks_host != "") {
					do_resolve_proxy(it->endpoint().address().to_string());
				} else {
					do_connect(it);
				}
			} else {
				cout << "error async_resolve" << endl;
			}
		});
	}
	void do_connect(ip::tcp::resolver::iterator it) {
		auto self(shared_from_this());

		_socket.async_connect(*it, [this, self](const boost::system::error_code& ec) {
			if (!ec) {
				cout << "connect success" << endl;
				_data.fill('\0');
				do_read();
			} else {
				cout << "error async_connect" << endl;
			}
		});
	}
	void do_read() {
		auto self(shared_from_this());

		_socket.async_read_some(buffer(_data, max_length), [this, self](boost::system::error_code ec, size_t length) {
			string recvData((char *)_data.data());
			cout << recvData << flush;
			auto result = recvData.find("% ");
			if (!ec) {
				if (result != std::string::npos) { // find %
					printHTMLAdd(recvData, _id, !ISPROMPT);
					do_send_prompt();
				} else {
					printHTMLAdd(recvData, _id, !ISPROMPT);
					_data.fill('\0');
					do_read();
				}
			} else {
				_socket.close();
			}
		});
	}
	void do_send_prompt() {
		auto self(shared_from_this());
		/* specify the next command */
		string nextCommand;
		getline(_file, nextCommand);
		cout << nextCommand << flush;
		nextCommand += "\n";
		printHTMLAdd(nextCommand, _id, ISPROMPT);
		_socket.async_send(buffer(nextCommand, nextCommand.size()), [this, self](const boost::system::error_code& ec, size_t bytes_transferred) {
			if (!ec) {
				_data.fill('\0');
				do_read();
			} else {
				_socket.close();
			}
		});
	}
};

int main() {
	cout << "Content-type: text/html" << endl << endl;
	/* section: parse query */
	string query(getenv("QUERY_STRING"));
	vector<string> splitedQuery;
	map<string, string> queryMap;
	boost::split(splitedQuery, query, boost::is_any_of("&"), boost::token_compress_on);
	for (int i = 0; i < splitedQuery.size(); i++) {
		vector<string> kvPair;
		boost::split(kvPair, splitedQuery[i], boost::is_any_of("="), boost::token_compress_on);
		if (kvPair.size() == 1) {
			queryMap.insert(pair<string, string>(kvPair[0], ""));
		} else {
			queryMap.insert(pair<string, string>(kvPair[0], kvPair[1]));
		}
	}
	/* section: print HTML framework */
	printHTMLStructure(queryMap);
	/* section: connect to servers */
	for (auto iter = queryMap.begin(); iter != queryMap.end(); iter++) {
	}
	try {
		int id = 0;
		if (queryMap["sh"] != "") {
			for (int i = 0; i < 5; i++) {
				if (queryMap["h" + to_string(i)] != "") {
					make_shared<ShellClient>(queryMap["sh"], queryMap["sp"], queryMap["h" + to_string(i)], queryMap["p" + to_string(i)], queryMap["f" + to_string(i)], id)->start();
					id++;
				}
			}
		} else {
			for (int i = 0; i < 5; i++) {
				if (queryMap["h" + to_string(i)] != "") {
					make_shared<ShellClient>("", "", queryMap["h" + to_string(i)], queryMap["p" + to_string(i)], queryMap["f" + to_string(i)], id)->start();
					id++;
				}
			}
		}
		global_io_service.run();
	} catch (exception& e) {
		cout << "Exception: " << e.what() << "\n" << flush;
	}
	return 0;
}
