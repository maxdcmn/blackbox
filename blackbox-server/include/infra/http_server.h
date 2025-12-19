#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/http.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

void handleRequest(http::request<http::string_body>& req, tcp::socket& socket);
void acceptConnections(tcp::acceptor& acceptor);

