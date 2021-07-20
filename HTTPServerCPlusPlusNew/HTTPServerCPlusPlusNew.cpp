// HTTPServerCPlusPlusNew.cpp : Этот файл содержит функцию "main". Здесь начинается и заканчивается выполнение программы.
//

#include <iostream>
#include <string_view>
#include <sstream>
#include <filesystem>
#include <memory>
#include <regex>

#include <boost/program_options.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <boost/algorithm/string.hpp>

#include <windows.h>

namespace bp = boost::program_options;
namespace ip = boost::asio::ip;
using tcp = boost::asio::ip::tcp;
namespace bb = boost::beast;
namespace http = boost::beast::http;
namespace pt = boost::property_tree;
namespace fs = std::filesystem;

template<bool isRequest, typename Body, typename Fields>
void send_(http::message<isRequest, Body, Fields>&& msg, tcp::socket& sock, boost::asio::yield_context& yield, boost::system::error_code& ec) {
	http::serializer<isRequest, Body, Fields> sr{ msg };
	http::async_write(sock, sr, yield[ec]);
}

std::wstring utf8_decode(const std::string& str);
std::string utf8_encode(const std::wstring& wstr);

template<typename T>
bool fail_(tcp::socket& sock, const http::request<T>& req, boost::asio::yield_context& yield, boost::system::error_code& ec) {
	std::string_view sv(req.target().data(), req.target().size());
	if (sv.empty() || sv[0] != '/' || sv.find("..") != std::string_view::npos) {
		http::response<http::string_body> res{ http::status::bad_request, req.version() };
		res.set(http::field::content_type, "text/plain");
		res.body() = "Bad request";
		res.prepare_payload();
		send_(std::move(res), sock, yield, ec);
		return true;
	}
	else {
		return false;
	}
}

int main(int argc, char* argv[])
{
	std::ios::sync_with_stdio(false);
	std::string root_dir;
	bp::options_description desc("Usage");
	desc.add_options()
		("help,h", "show this help message")
		("root_dir,d", bp::value<std::string>(&root_dir)->default_value(std::filesystem::current_path().string()), "root directory to share over HTTP")
		("ip", bp::value<std::string>()->default_value("127.0.0.1"), "ip to serve")
		("port,p", bp::value<uint16_t>()->default_value(8080), "port to serve");
	bp::variables_map vm;
	store(bp::parse_command_line(argc, argv, desc), vm);
	notify(vm);

	if (vm.count("help")) {
		std::cout << desc << std::endl;
		return 0;
	}

	fs::path root_dir_p{ root_dir };

	boost::asio::io_context ioc;

	boost::asio::signal_set signals(ioc, SIGTERM, SIGINT);

	signals.async_wait([&](auto, auto) { std::cout << "sigaction" << std::endl; ioc.stop(); });

	boost::asio::spawn(ioc, [&](boost::asio::yield_context yield) {
		auto fail = [](const boost::system::error_code& ec, std::string_view sv) {
			std::cerr << sv << ": " << ec.message() << std::endl;
		};
		ip::address ipToListen{ ip::make_address(vm["ip"].as<std::string>()) };
		tcp::endpoint ep{ ipToListen, vm["port"].as<uint16_t>() };
		tcp::acceptor acc(ioc);
		boost::system::error_code ec;
		acc.open(ep.protocol());
		acc.set_option(boost::asio::socket_base::reuse_address(true));
		acc.bind(ep);
		acc.listen();

		for (;;) {
			auto sock = std::make_shared<tcp::socket>(ioc);
			auto clientIp = std::make_shared<tcp::endpoint>();
			acc.async_accept(*sock, *clientIp, yield[ec]);

			if (ec) {
				std::cerr << "ERROR: accept failed: " << ec.message() << std::endl;
				ec.clear();
			}
			else {
				boost::asio::spawn(acc.get_executor(), [sock, clientIp, root_dir, root_dir_p](boost::asio::yield_context yield) {
					std::cout << "clientIp: " << *clientIp << std::endl;
					boost::system::error_code ec2;
					boost::beast::flat_buffer buf;
					http::request<http::string_body> req;
					http::async_read(*sock, buf, req, yield[ec2]);

					if (ec2) {
						std::cerr << "ERROR: async_read failed: " << ec2.message() << std::endl;
					}
					else {
						std::cout << "New request:\n" << req << std::endl;
						std::string_view sv(req.target().data(), req.target().size());
						fs::path path_target(root_dir_p);
						path_target += sv;
						switch (req.method()) {
						case http::verb::get: {
							if (fail_(*sock, req, yield, ec2)) {}
							else if (sv == "/") {
								pt::ptree root;
								pt::ptree files;
								for (const auto& p : fs::directory_iterator(root_dir_p)) {
									pt::ptree ch;
									ch.put("", p.path().filename().string());
									files.push_back(std::make_pair("", std::move(ch)));
								}
								root.add_child("files", std::move(files));

								std::ostringstream oss;
								pt::write_json(oss, root);

								http::response<http::string_body> res{ http::status::ok, req.version() };
								res.set(http::field::content_type, "application/json");
								res.body() = oss.str();
								res.prepare_payload();
								send_(std::move(res), *sock, yield, ec2);
							}
							else {
								if (fs::exists(path_target)) {
									std::cout << "target found: " << path_target << std::endl;
									std::wstring wt = path_target.wstring();
									http::response<http::file_body> res{ http::status::ok, req.version() };
									//res.set(http::field::content_type, "");
									std::string t2 = utf8_encode(wt);
									res.body().open(t2.c_str(), bb::file_mode::scan, ec2);
									if (ec2) {
										std::cerr << "ERROR: res.body().open() returned: " << ec2.message() << std::endl;//utf8_decode(ec2.message()) << std::endl;
										std::ofstream output("debug_text.txt", std::ios::binary);
										output << ec2.message();
										output.close();
									}
									else {
										res.prepare_payload();
										send_(std::move(res), *sock, yield, ec2);
										std::cout << "target: " << std::quoted(sv) << std::endl;
										std::cout << "" << std::quoted(sv) << std::endl;
									}
								}
								else {
									std::cout << "target not found: " << path_target << std::endl;
									http::response<http::string_body> res{ http::status::not_found, req.version() };
									res.body() = "Not found";
									res.prepare_payload();
									send_(std::move(res), *sock, yield, ec2);
								}
							}
							break;
						}

						case http::verb::post: {
							std::cout << "POST STAGE 1" << std::endl;
							if (fail_(*sock, req, yield, ec2)) { std::cout << "POST STAGE 2" << std::endl; }
							else if (fs::is_directory(path_target)) {
								std::cout << "POST STAGE 3" << std::endl;
								http::response<http::string_body> res{ http::status::bad_request, req.version() };
								res.body() = "Bed request: specified uri is a directory";
								send_(std::move(res), *sock, yield, ec2);
							}
							else {
								std::cout << "POST STAGE 4" << std::endl;
								std::cout << "creating file" << std::endl;
								std::ofstream fout(path_target, std::ios::binary);

								fout.write(req.body().c_str(), req.body().size());

								if (fout.good()) {
									http::response<http::empty_body> res{ http::status::created, req.version() };
									send_(std::move(res), *sock, yield, ec2);
								}
								else {
									http::response<http::string_body> res{ http::status::internal_server_error, req.version() };
									res.body() = "Internal server error: i/o error";
									send_(std::move(res), *sock, yield, ec2);
								}
							}

							break;
						}

						case http::verb::delete_: {
							if (fail_(*sock, req, yield, ec2)) {}
							else if (fs::is_regular_file(path_target)) {
								if (fs::remove(path_target)) {
									http::response<http::empty_body> res(http::status::ok, req.version());
									send_(std::move(res), *sock, yield, ec2);
								}
								else {
									http::response<http::string_body> res(http::status::internal_server_error, req.version());
									res.body() = "Internal server error: remove failed";
									send_(std::move(res), *sock, yield, ec2);
								}
							}
							else {
								http::response<http::string_body> res(http::status::bad_request, req.version());
								res.body() = "Bad request: it is not a regular file";
								send_(std::move(res), *sock, yield, ec2);
							}
							break;
						}

						default: {
							http::response<http::string_body> res(http::status::bad_request, req.version());
							res.body() = "Unknown method";
							send_(std::move(res), *sock, yield, ec2);
						}
						}
						std::cout << "Request was handled" << std::endl;
					}

					sock->shutdown(tcp::socket::shutdown_both, ec2);
					if (ec2) {
						std::cerr << "ERROR: shutdown failed: " << ec2.message() << std::endl;
					}
					sock->close();
					});
			}
		}

		});

	try {
		ioc.run();
	}
	catch (const std::exception& ex) {
		std::cerr << "ERROR: " << ex.what() << std::endl;
	}
	return 0;
}

std::string utf8_encode(const std::wstring& wstr) {
	if (wstr.empty()) return std::string();
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
	std::string str(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), str.data(), size_needed, NULL, NULL);
	return str;
}

std::wstring utf8_decode(const std::string& str) {
	if (str.empty()) return std::wstring();
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), NULL, 0);
	std::wstring wstr(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), wstr.data(), size_needed);
	return wstr;
}
