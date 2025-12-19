#pragma once

#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

void handleSpindownRequest(http::request<http::string_body>& req, tcp::socket& socket);
void handleListModelsRequest(http::request<http::string_body>& req, tcp::socket& socket);






