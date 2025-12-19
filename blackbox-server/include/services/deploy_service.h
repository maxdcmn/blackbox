#pragma once

#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include "hf_deploy.h"

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

void handleDeployRequest(http::request<http::string_body>& req, tcp::socket& socket);






