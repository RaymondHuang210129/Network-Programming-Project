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
#include <fstream>
#include <memory>
#include <utility>

#define CD_REQUEST 1
#define CD_REPLY_GRANTED 90
#define CD_REPLY_REJECTED 91
#define MODE_CONNECT 1
#define MODE_BIND 2

using namespace std;
using namespace boost::asio;

io_service global_io_service;

class SockTransmission : public enable_shared_from_this<SockTransmission> {
private:
	enum { max_length = 65536 };
	ip::tcp::socket _server_socket;
	ip::tcp::socket _client_socket;
	array<unsigned char, max_length> _forward_data, _backward_data;
public:
	SockTransmission(ip::tcp::socket send_socket, ip::tcp::socket receive_socket) : _server_socket(move(receive_socket)),
	                                                                                _client_socket(move(send_socket)) {}
	void start() {
		forward();
		backward();
		cout << "hi" << endl;
	}
private:
	void forward() {
		auto self(shared_from_this());
		_server_socket.async_read_some(
			buffer(_forward_data, max_length),
			[this, self](boost::system::error_code ec, size_t length) {
				if (!ec && length != 0) {
					cout << "packet send from " << _server_socket.remote_endpoint().address().to_string() << " to " << _client_socket.remote_endpoint().address().to_string() << endl;
					_client_socket.async_send(
						buffer(_forward_data, length), 
						[this, self](const boost::system::error_code& ec, size_t bytes_transferred) {
							if (!ec && bytes_transferred != 0) {
								_forward_data.fill('\0');
								forward();
							} else {
								cout << "error: " << ec.message() << endl;
								_client_socket.close();
							}
						});
				} else {
					cerr << "error: " << "forward" << endl;
					//_server_socket.close();
				}
			});
	}

	void backward() {
		auto self(shared_from_this());
		_client_socket.async_read_some(
			buffer(_backward_data, max_length),
			[this, self](boost::system::error_code ec, size_t length) {
				if (!ec && length != 0) {
					cout << "packet send from " << _client_socket.remote_endpoint().address().to_string() << " to " << _server_socket.remote_endpoint().address().to_string() << endl;
					_server_socket.async_send(
						buffer(_backward_data, length),
						[this, self](const boost::system::error_code& ec, size_t bytes_transferred) {
							if (!ec && bytes_transferred != 0) {
								_backward_data.fill('\0');
								backward();
							} else {
								cout << "error: " << ec.message() << endl;
								_server_socket.close();
							}
						});
				} else {
					cerr << "error: " << "backward" << endl;
					//_client_socket.close();
				}
			});
	}
};

class SockSession : public enable_shared_from_this<SockSession> {
private:
	enum { max_length = 65536 };
	ip::tcp::socket _socket;
	array<unsigned char, max_length> _data;
	vector<string> bind_rules;
	vector<string> connect_rules;


public:
	SockSession(ip::tcp::socket socket) : _socket(move(socket)) {
		fstream file("./socks.conf", fstream::in);
		string line;
		while(getline(file, line)) {
			if (line[7] == 'c') {
				connect_rules.push_back(strtok(const_cast<char*>(line.substr(9).c_str()), "*"));
			} else if (line[7] == 'b') {
				bind_rules.push_back(strtok(const_cast<char*>(line.substr(9).c_str()), "*"));
			}
		}
		file.close();
	}
	void start() {
		_data.fill('\0');
		do_read_request();
	}

private:
	void do_read_request() {
		auto self(shared_from_this());
		try {
		_socket.async_read_some(
			buffer(_data, max_length),
			[this, self](boost::system::error_code ec, size_t length) {
				if (!ec && length) {
					/* section: read finished */
					unsigned char* result = _data.data();
					if (is_sock(result, strlen((char*)result))) {
						unsigned short dstport = result[2] * 256 + result[3];
						string dstaddr("" + to_string((unsigned int)result[4]) + "." + to_string((unsigned int)result[5]) + "." + to_string((unsigned int)result[6]) + "." + to_string((unsigned int)result[7]));
						cout << "<S_IP>: " << _socket.remote_endpoint().address().to_string() << endl;
						cout << "<S_PORT>: " << _socket.remote_endpoint().port() << endl;
						cout << "<D_IP>: " << dstaddr << endl;
						cout << "<D_PORT>: " << dstport << endl;
						cout << "<Command>: " << "CONNECT" << endl;
						if (is_permit(_socket.remote_endpoint().address().to_string(), MODE_CONNECT)) {
							cout << "<Reply>: " << "Accept" << endl;
							do_reply(CD_REPLY_GRANTED, dstport, dstaddr);
						} else {
							cout << "<Reply>: " << "Reject" << endl;
							do_reply(CD_REPLY_REJECTED, 0, "");
						}
					} else {
						cout << "not the sock4 request." << endl;
					}
				} else {
					cerr << "error: " << "do_read_request" << ec.message() << endl;
				}
			});
			} catch (exception& e) {
				cout << e.what() << endl;
			}
	}

	void do_reply(int command, unsigned short server_port, string server_addr) {
		auto self(shared_from_this());
		if (command == CD_REPLY_GRANTED) {
			unsigned char reply[8] = {0, 90, 0, 0, 0, 0, 0, 0};
			_socket.async_send(
				buffer((void *)reply, (size_t)8), [this, self, server_addr, server_port](const boost::system::error_code& ec, size_t bytes_transferred) {
					if (!ec) {
						ip::tcp::socket server_socket(global_io_service);
						//shared_ptr<ip::tcp::socket> server_socket;
						//server_socket = make_shared<ip::tcp::socket>(global_io_service);
						ip::tcp::endpoint server_endpoint(ip::address::from_string(server_addr), server_port);
						server_socket.connect(server_endpoint);
						make_shared<SockTransmission>(move(_socket), move(server_socket))->start();
					} else {
						cerr << "error: " << "do_reply_granted" << endl;
					}
				});
		} else if (command == CD_REPLY_REJECTED){
			unsigned char reply[8] = {0, 91, 0, 0, 0, 0, 0, 0};
			_socket.async_send(
				buffer((void *)reply, (size_t)8), [this, self](const boost::system::error_code& ec, size_t bytes_transferred){
					if (!ec) {
						_socket.close();
					} else {
						cerr << "error: " << "do_reply_rejected" << endl;
					}
				});
		}
		
	}

	bool is_sock(unsigned char* result, int length) {
		if (result[0] == 4 && result[1] == CD_REQUEST) { /* note: connect */
			return true;
		} else {
			return false;
		}
	}
	bool is_permit(string address, int mode) {
		if (mode == MODE_CONNECT) {
			for (int i = 0; i < connect_rules.size(); i++) {
				if (address.substr(0, connect_rules[i].length()) == connect_rules[i]) {
					return true;
				}
			}
		} else {
			for (int i = 0; i < bind_rules.size(); i++) {
				if (address.substr(0, bind_rules[i].length()) == bind_rules[i]) {
					return true;
				}
			}
		}
		return false;
	}
};

class SockServer : enable_shared_from_this<SockServer> {
private:
	enum { max_length = 65536 };
	ip::tcp::acceptor _acceptor;
	ip::tcp::socket _socket;
public:
	SockServer(short port) : _acceptor(global_io_service, ip::tcp::endpoint(ip::tcp::v4(), port)),
	                         _socket(global_io_service) {
		do_accept();
	}
private:
	void do_accept() {
		_acceptor.async_accept(_socket, [this](boost::system::error_code ec) {
			pid_t cpid;
			if (!ec) {
				/* section: fork */
//				global_io_service.notify_fork(io_service::fork_prepare);
//				cpid = fork();
//				if (cpid == 0) { /* note: child proess */
//					global_io_service.notify_fork(io_service::fork_child);
					make_shared<SockSession>(move(_socket))->start();
//					exit(0);
//				} else { /* parent process */
//					global_io_service.notify_fork(io_service::fork_parent);
//					_socket.close();
//				}
			} else {
				cout << "error: " << ec.message() << endl;
			}
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
		SockServer server(port);
		global_io_service.run();
	} catch (exception& e) {
		cerr << "Exception: " << e.what() << endl;
	}
	return 0;
}