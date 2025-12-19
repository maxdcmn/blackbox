#include "services/spindown_service.h"
#include "services/model_manager.h"
#include "utils/json_parser.h"
#include <nlohmann/json.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <sstream>
#include <string>

void handleSpindownRequest(http::request<http::string_body>& req, tcp::socket& socket) {
    std::string body = req.body();
    std::string model_id = parseJSONField(body, "model_id");
    std::string container_id = parseJSONField(body, "container_id");
    
    http::response<http::string_body> res;
    res.version(req.version());
    res.keep_alive(req.keep_alive());
    res.set(http::field::content_type, "application/json");
    
    std::string target = model_id.empty() ? container_id : model_id;
    if (target.empty()) {
        res.result(http::status::bad_request);
        res.body() = R"({"success":false,"message":"model_id or container_id is required"})";
        res.prepare_payload();
        http::write(socket, res);
        return;
    }
    
    bool success = spindownModel(target);
    
    nlohmann::json response_json;
    if (success) {
        res.result(http::status::ok);
        response_json["success"] = true;
        response_json["message"] = "Model spindown successful";
        response_json["target"] = target;
    } else {
        res.result(http::status::internal_server_error);
        response_json["success"] = false;
        response_json["message"] = "Failed to spindown model: " + target;
    }
    res.body() = response_json.dump();
    
    res.prepare_payload();
    try {
        http::write(socket, res);
    } catch (const boost::system::system_error& e) {
        auto ec = e.code();
        if (ec == boost::asio::error::broken_pipe || 
            ec == boost::asio::error::connection_reset ||
            ec == boost::asio::error::eof) {
            return;
        }
        throw;
    }
}

void handleListModelsRequest(http::request<http::string_body>& req, tcp::socket& socket) {
    std::vector<DeployedModel> models = listDeployedModels();
    int max_allowed = getMaxConcurrentModels();
    int running = 0;
    for (const auto& m : models) {
        if (m.running) running++;
    }
    
    http::response<http::string_body> res;
    res.version(req.version());
    res.keep_alive(req.keep_alive());
    res.result(http::status::ok);
    res.set(http::field::content_type, "application/json");
    
    nlohmann::json response_json;
    response_json["total"] = models.size();
    response_json["running"] = running;
    response_json["max_allowed"] = max_allowed;
    
    nlohmann::json models_array = nlohmann::json::array();
    for (const auto& model : models) {
        nlohmann::json model_json;
        model_json["model_id"] = model.model_id;
        model_json["container_id"] = model.container_id;
        model_json["container_name"] = model.container_name;
        model_json["port"] = model.port;
        model_json["running"] = model.running;
        models_array.push_back(model_json);
    }
    response_json["models"] = models_array;
    
    res.body() = response_json.dump();
    res.prepare_payload();
    
    try {
        http::write(socket, res);
    } catch (const boost::system::system_error& e) {
        auto ec = e.code();
        if (ec == boost::asio::error::broken_pipe || 
            ec == boost::asio::error::connection_reset ||
            ec == boost::asio::error::eof) {
            return;
        }
        throw;
    }
}

