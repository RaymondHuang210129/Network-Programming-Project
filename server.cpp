#include <array>
#include <string>
#include <vector>
#include <iterator>
#include <map>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/regex.hpp>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <memory>
#include <utility>

using namespace std;
using namespace boost::asio;

io_service global_io_service;

class HTTPSession : public enable_shared_from_this<HTTPSession> {
private:
	enum { max_length = 1024 };
	ip::tcp::socket _socket;
	array<char, max_length> _data;

public:
	HTTPSession(ip::tcp::socket socket) : _socket(move(socket)) {}
	void start() { do_read(); }

private:
	void do_read() {
		auto self(shared_from_this());
		_socket.async_read_some(
			buffer(_data, max_length),
			[this, self](boost::system::error_code ec, size_t length) {
				if (!ec) {
					map<string, string> envmap = header_parser(_data.data());
					cout << "start fork" << endl;
					/* section: fork */
					pid_t cpid = fork();
					if (cpid == 0) {
						for (auto iter = envmap.begin(); iter != envmap.end(); iter++) {
							setenv(iter->first.c_str(), iter->second.c_str(), 1);
						}
						auto native = _socket.native_handle();
						close(STDIN_FILENO);
						close(STDOUT_FILENO);
						dup2(native, STDIN_FILENO);
						dup2(native, STDOUT_FILENO);
						cout << "HTTP/1.1 200 OK\r\n";
						char* execVect[2];
						string cgiPath = "." + envmap["REQUEST_URI"];
						execVect[0] = strdup(cgiPath.c_str());
						execVect[1] = NULL;
						execvp(execVect[0], execVect);
						if (errno == 2) {
							cerr << "Unknown URI: [" << execVect[0] << "]." << endl;
						}
						else {
							cerr << "exec error: " << errno << endl;
						}
					} else {
						cout << "closing parent socket" << endl;
						_socket.close();
					}
				}
			}
		);
	}

	map<string, string> header_parser(char* rawData) {
		string data(rawData);
		vector<string> splitedHeader;
		split_regex(splitedHeader, data, boost::regex("(\r\n)+"));
		cout << splitedHeader.size() << endl;
		map<string, string> envmap;
		vector<string> splitedHeaderL1;
		boost::split(splitedHeaderL1, splitedHeader[0], boost::is_any_of(" "), boost::token_compress_on);
		envmap.insert(pair<string, string>("REQUEST_METHOD", splitedHeaderL1[0]));
		vector<string> tmp;

		boost::split(tmp, splitedHeaderL1[1], boost::is_any_of("?"), boost::token_compress_on);
		envmap.insert(pair<string, string>("REQUEST_URI", tmp[0]));
		if (tmp.size() == 1) {
			envmap.insert(pair<string, string>("QUERY_STRING", ""));
		} else {
			envmap.insert(pair<string, string>("QUERY_STRING", tmp[1]));
		}
		envmap.insert(pair<string, string>("SERVER_PROTOCOL", splitedHeaderL1[2]));
		envmap.insert(pair<string, string>("HTTP_HOST", splitedHeader[1].substr(6)));
		envmap.insert(pair<string, string>("SERVER_ADDR", _socket.local_endpoint().address().to_string()));
		envmap.insert(pair<string, string>("SERVER_PORT", to_string(_socket.local_endpoint().port())));
		envmap.insert(pair<string, string>("REMOTE_ADDR", _socket.remote_endpoint().address().to_string()));
		envmap.insert(pair<string, string>("REMOTE_PORT", to_string(_socket.remote_endpoint().port())));
		for (auto iter = envmap.begin(); iter != envmap.end(); iter++) {
			cout << iter->first << ": " << iter->second << endl;
		}
		return envmap;
	}
};

class HTTPServer {
private:
	ip::tcp::acceptor _acceptor;
	ip::tcp::socket _socket;

public:
	HTTPServer(short port) : _acceptor(global_io_service, ip::tcp::endpoint(ip::tcp::v4(), port)),
	                         _socket(global_io_service) {
		do_accept();
	}

private:
	void do_accept() {
		_acceptor.async_accept(_socket, [this](boost::system::error_code ec) {
			if (!ec) {make_shared<HTTPSession>(move(_socket))->start();}
			do_accept();
		});
	}
};

int main(int argc, char* const argv[]) {
	if (argc != 2) {
		cerr << "Incorrect parameters." << endl;
		exit(1);
	}

	try {
		unsigned short port = atoi(argv[1]);
		HTTPServer server(port);
		global_io_service.run();
	} catch (exception& e) {
		cerr << "Exception: " << e.what() << "\n";
	}

	return 0;
}