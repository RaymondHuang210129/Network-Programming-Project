#include <array>
#include <string>
#include <vector>
#include <iterator>
#include <map>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/regex.hpp>
//#include <boost/algorithm/string/split.hpp>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <memory>
#include <utility>

using namespace std;
using namespace boost::asio;

io_service global_io_service;

string printPanel() {
	string msg("HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\r\n<!DOCTYPE html><html lang='en'><head><title>NP Project 3 Panel</title><link rel='stylesheet' href='https://stackpath.bootstrapcdn.com/bootstrap/4.1.3/css/bootstrap.min.css' integrity='sha384-MCw98/SFnGE8fJT3GXwEOngsV7Zt27NXFoaoApmYm81iuXoPkFOJwJ8ERdknLPMO' crossorigin='anonymous'/><link href='https://fonts.googleapis.com/css?family=Source+Code+Pro' rel='stylesheet'/><link rel='icon' type='image/png' href='https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png'/><style> * { font-family: 'Source Code Pro', monospace; }</style></head><body class='bg-secondary pt-5'><form action='GET' method='console.cgi'><table class='table mx-auto bg-light' style='width: inherit'><thead class='thead-dark'><tr><th scope='col'>#</th><th scope='col'>Host</th><th scope='col'>Port</th><th scope='col'>Input File</th></tr></thead><tbody>");

	for (int i = 0; i < 5; i++) {
		msg += ("<tr><th scope='row' class='align-middle'>Session " + to_string(i + 1) + "</th><td><div class='input-group'><select name='h" + to_string(i) + "' class='custom-select'><option></option>");

		for (int j = 0; j < 12; j++) {
			msg += ("<option value='nplinux" + to_string(j + 1) + ".cs.nctu.edu.tw'>nplinux" + to_string(j + 1) + "</option>");
		}
		msg += ("</select><div class='input-group-append'><span class='input-group-text'>.cs.nctu.edu.tw</span></div></div></td><td><input name='p" + to_string(i) + "' type='text' class='form-control' size='5' /></td><td><select name='f" + to_string(i) + "' class='custom-select'><option></option>");
		for (int j = 0; j < 10; j++) {
			msg += ("<option value='t" + to_string(j + 1) + ".txt'>t" + to_string(j + 1) + ".txt</option>");
		}
		msg += ("</select></td></tr>");
	}
	msg += ("<tr><td colspan='3'></td><td><button type='submit' class='btn btn-info btn-block'>Run</button></td></tr></tbody></table></form></body></html>");
	return msg;
}

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
	/*
	if (isPrompt) {
		cout << "<script>document.getElementById('s" << id << "').innerHTML += '" << recvData << "';</script>" << flush;
	} else {
		cout << "<script>document.getElementById('s" << id << "').innerHTML += '<b>" << recvData << "</b>';</script>" << flush;
	}
	*/
}

void printConsole(map<string, string> envmap) {

}

class HTTPSession : public enable_shared_from_this<HTTPSession> {
private:
	enum { max_length = 1024 };
	ip::tcp::socket _socket;
	//strand<io_context::executer_type> _strand;
	array<char, max_length> _data;
	//string const& _doc_root;
	//http::request<http::string_body> _request;
	//shared_ptr<void> _result;

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

					if (envmap["REQUEST_URI"] == "/panel.cgi") {
						do_send(printPanel());

					} else if (envmap["REQUEST_URI"] == "/console.cgi") {
						printConsole(envmap);
					}
				}
			}
		);
	}

	void do_send(string msg) {
		auto self(shared_from_this());
		_socket.async_send(buffer(msg, msg.size()), [this, self](const boost::system::error_code& ec, size_t bytes_transferred) {
			if (!ec) {

			}
		});
	}

	map<string, string> header_parser(char* rawData) {
		string data(rawData);
		vector<string> splitedHeader;
		split_regex(splitedHeader, data, boost::regex("(\r\n)+"));
		cout << splitedHeader.size() << endl;
		//for (int i = 0; i < splitedHeader.size(); i++) {
		//	cout << i << ":" << splitedHeader[i] << endl;
		//}
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