#include <array>
#include <string>
#include <vector>
#include <iterator>
#include <map>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/exception/all.hpp> 
#include <boost/regex.hpp>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <fstream>
#include <memory>
#include <utility>
#include <sys/wait.h>

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
	enum { max_length = 1000000 };
	ip::tcp::socket& _server_socket;
	ip::tcp::socket& _client_socket;
	array<unsigned char, max_length> _data;
public:
	SockTransmission(ip::tcp::socket& send_socket, ip::tcp::socket& receive_socket) : _server_socket(receive_socket),
	                                                                                _client_socket(send_socket) {}
	void start() {
		//cout << "start data transfer" << endl;
		forward(_client_socket, _server_socket);
	}
private:
	void forward(ip::tcp::socket& read_socket, ip::tcp::socket& write_socket) {
		auto self(shared_from_this());
		try {
			//cout << "wait for reading" << endl;
			read_socket.async_read_some(
				buffer(_data, max_length),
				[this, self, &read_socket, &write_socket](boost::system::error_code ec, size_t length) {
					if (!ec) {
						//cout << "packet send from " << read_socket.remote_endpoint().address().to_string() << " to " << write_socket.remote_endpoint().address().to_string() << endl;
						size_t result = write(write_socket, buffer(_data, length));
						if (result != length) {
							cout << result << " " << length << endl;
						}
						//_data.fill('\0');
						//write(write_socket, buffer(_data, length));
						forward(read_socket, write_socket);
					} else {
						//cout << "end" << endl;
						//cerr << "error: async_receive" << ec.message() << endl;
						read_socket.cancel();
						write_socket.cancel();
						exit(1);
					}
				});
		} catch (boost::system::system_error &e) {
			cout << "error" << endl;
			cout << boost::diagnostic_information(e) << endl;
		}
		
	}

};

void signalHandler(int sigNum) {
	int status;
	while(waitpid(-1, &status, WNOHANG) > 0) {}
	//cout << "rrrr" << endl;
	signal(SIGCHLD, signalHandler);
	return;
}

struct rule {
	unsigned int addr = 0;
	unsigned int mask = 0;
	rule(unsigned int _addr, unsigned int _mask) : addr(_addr), mask(_mask) {};
};

void register_rules(vector<rule>& bind_rules, vector<rule>& connect_rules) {
	fstream file("./socks.conf", fstream::in);
		string line;
		while(getline(file, line)) {
			vector<string> splited_addr;
			string subline = line.substr(9);
			boost::split(splited_addr, subline, boost::is_any_of("."), boost::token_compress_on);
			unsigned int addr = 0;
			unsigned int mask = 0;
			for (int i = 0; i < 4; i++) {
				if (splited_addr[i] == "*") {
					break;
				} else {
					addr += (stoi(splited_addr[i]) << (8 * (3 - i)));
					mask += (255 << (8 * (3 - i)));
				}
			}
			if (line[7] == 'c') {
				connect_rules.push_back(rule(addr, mask));
			} else if (line[7] == 'b') {
				bind_rules.push_back(rule(addr, mask));
			}
		}
		file.close();
}

bool is_permit(string address, vector<rule> rules) {
	vector<string> splited_addr;
	boost::split(splited_addr, address, boost::is_any_of("."), boost::token_compress_on);
	unsigned int test_addr = 0;
	for (int i = 0; i < 4; i++) {
		test_addr += ((unsigned int)stoi(splited_addr[i]) << (8 * (3 - i)));
	}
	for (int i = 0; i < rules.size(); i++) {
		if ((test_addr & rules[i].mask) == (rules[i].addr & rules[i].mask)) {
			return true;
		}
	}
	return false;
}

void reply(ip::tcp::socket& client_socket, int command, unsigned short port) {
	if (command == CD_REPLY_GRANTED) {

		unsigned char reply[8] = {0, CD_REPLY_GRANTED, (unsigned char)(port >> (unsigned short)8), (unsigned char)(port & (unsigned short)255), 0, 0, 0, 0};
		write(client_socket, buffer((void*)reply, (size_t)8));
		//client_socket.send(buffer((void*)reply, (size_t)8));
	} else {
		unsigned char reply[8] = {0, CD_REPLY_REJECTED, 0, 0, 0, 0, 0, 0};
		write(client_socket, buffer((void*)reply, (size_t)8));
		//client_socket.send(buffer((void*)reply, (size_t)8));
	}
}

void socket_session(ip::tcp::socket& client_socket) {
	vector<rule> bind_rules;
	vector<rule> connect_rules;
	enum { max_length = 1000000 };
	array<unsigned char, max_length> _data;
	register_rules(bind_rules, connect_rules);
	boost::system::error_code ec;
	client_socket.read_some(buffer(_data, max_length), ec);
	if (!ec) {
		unsigned char* result = _data.data();
		unsigned short dstport = result[2] * 256 + result[3];
		string dstaddr("" + to_string((unsigned int)result[4]) + "." + to_string((unsigned int)result[5]) + "." + to_string((unsigned int)result[6]) + "." + to_string((unsigned int)result[7]));
		cout << "<S_IP>: " << client_socket.remote_endpoint().address().to_string() << endl;
		cout << "<S_PORT>: " << client_socket.remote_endpoint().port() << endl;
		cout << "<D_IP>: " << dstaddr << endl;
		cout << "<D_PORT>: " << dstport << endl;
		if (result[1] == MODE_CONNECT) {
			cout << "<Command>: " << "CONNECT" << endl;
			if (is_permit(client_socket.remote_endpoint().address().to_string(), connect_rules)) {
				cout << "<Reply>: " << "Accept" << endl;
				reply(client_socket, CD_REPLY_GRANTED, 0);
				ip::tcp::endpoint server_endpoint(ip::address::from_string(dstaddr), dstport);
				ip::tcp::socket server_socket(global_io_service);
				server_socket.connect(server_endpoint);
				make_shared<SockTransmission>(client_socket, server_socket)->start();
				make_shared<SockTransmission>(server_socket, client_socket)->start();
				global_io_service.run();
				//cout << "socket should close" << endl;
			} else {
				cout << "<Reply>: " << "Reject" << endl;
				reply(client_socket, CD_REPLY_REJECTED, 0);
				client_socket.close();
			}
		} else {
			cout << "<Command>: " << "BIND" << endl;
			if (is_permit(client_socket.remote_endpoint().address().to_string(), bind_rules)) {
				cout << "<Reply>: " << "Accept" << endl;
				ip::tcp::endpoint bind_endpoint(ip::tcp::v4(), 0);
				ip::tcp::acceptor bind_acceptor(global_io_service, bind_endpoint);
				reply(client_socket, CD_REPLY_GRANTED, bind_acceptor.local_endpoint().port());
				ip::tcp::endpoint server_endpoint(ip::address::from_string(dstaddr), dstport);
				ip::tcp::socket server_socket(global_io_service);
				bind_acceptor.accept(server_socket);
				reply(client_socket, CD_REPLY_GRANTED, bind_acceptor.local_endpoint().port());
				make_shared<SockTransmission>(client_socket, server_socket)->start();
				make_shared<SockTransmission>(server_socket, client_socket)->start();
				global_io_service.run();
				//cout << "nonono" << endl;
			} else {
				cout << "<Reply>: " << "Reject" << endl;
				reply(client_socket, CD_REPLY_REJECTED, 0);
				client_socket.close();
			}
		}
	} else {
		//cout << "a socket is connected and closed immediately." << endl;
		//cout << ec.message() << endl;
		exit(0);
	}
}


int main(int argc, char* const argv[]) {
	if (argc != 2) {
		cerr << "Incorrect parameters." << endl;
		exit(1);
	}
	signal(SIGCHLD, signalHandler);
	ip::tcp::endpoint client_endpoint(ip::tcp::v4(), (unsigned int)atoi(argv[1]));
	ip::tcp::acceptor client_acceptor(global_io_service, client_endpoint);
	client_acceptor.set_option(ip::tcp::acceptor::reuse_address(true));
	while(1) {
		//cout << "log: wait for new request" << endl;
		ip::tcp::socket client_socket(global_io_service);
		client_acceptor.accept(client_socket);
		global_io_service.notify_fork(io_service::fork_prepare);
		while(true) {
			pid_t pid = fork();
			if (pid == 0) {
				global_io_service.notify_fork(io_service::fork_child);
				socket_session(client_socket);
				break;
			} else if (pid > 0) {
				global_io_service.notify_fork(io_service::fork_parent);
				client_socket.close();
				break;
				//cout << "forked" << endl;
			} else if (pid == -1){
				cout << "unable to fork" << endl;
				continue;
			}
		}
	}
	return 0;
}