#include "pointblank/ipc/IPCServer.hpp"
#include "pointblank/core/WindowManager.hpp"
#include "pointblank/layout/LayoutEngine.hpp"
#include "pointblank/window/FloatingWindowManager.hpp"

#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

namespace pblank {

bool IPCServer::start() {
    if (running_.load()) {
        return true;
    }
    
    
    size_t pos = socket_path_.find_last_of('/');
    if (pos != std::string::npos) {
        std::string dir = socket_path_.substr(0, pos);
        mkdir(dir.c_str(), 0755);
    }
    
    
    unlink(socket_path_.c_str());
    
    
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "IPC: Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }
    
    
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    
    sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
    
    if (bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "IPC: Failed to bind socket: " << strerror(errno) << std::endl;
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }
    
    
    chmod(socket_path_.c_str(), 0600);
    
    
    if (listen(server_fd_, 10) < 0) {
        std::cerr << "IPC: Failed to listen: " << strerror(errno) << std::endl;
        close(server_fd_);
        server_fd_ = -1;
        unlink(socket_path_.c_str());
        return false;
    }
    
    
    int flags = fcntl(server_fd_, F_GETFL, 0);
    fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK);
    
    running_.store(true);
    
    
    accept_thread_ = std::thread(&IPCServer::acceptLoop, this);
    
    std::cout << "IPC: Server started at " << socket_path_ << std::endl;
    return true;
}

void IPCServer::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
    
    
    unlink(socket_path_.c_str());
    
    
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        for (int fd : client_fds_) {
            close(fd);
        }
        client_fds_.clear();
    }
    
    
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    
    std::cout << "IPC: Server stopped" << std::endl;
}

void IPCServer::setCommandCallback(IPCCallback callback) {
    command_callback_ = std::move(callback);
}

void IPCServer::broadcast(const std::string& message) {
    std::lock_guard<std::mutex> lock(subscriber_mutex_);
    
    for (int fd : subscribers_) {
        send(fd, message.c_str(), message.length(), 0);
    }
}

void IPCServer::acceptLoop() {
    while (running_.load()) {
        sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd_, (sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            break;
        }
        
        
        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
        
        
        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            if (client_fds_.size() >= MAX_IPC_CLIENTS) {
                std::cerr << "IPC: Max clients reached (" << MAX_IPC_CLIENTS << "), rejecting connection" << std::endl;
                close(client_fd);
                continue;
            }
            client_fds_.push_back(client_fd);
        }
        
        
        std::thread([this, client_fd]() {
            handleClient(client_fd);
        }).detach();
    }
}

void IPCServer::handleClient(int client_fd) {
    
    thread_local char buffer[4096];
    std::string command_buffer;
    
    while (running_.load()) {
        ssize_t n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            break;
        }
        
        buffer[n] = '\0';
        command_buffer += buffer;
        
        
        size_t pos;
        while ((pos = command_buffer.find('\n')) != std::string::npos) {
            std::string command = command_buffer.substr(0, pos);
            command_buffer = command_buffer.substr(pos + 1);
            
            if (!command.empty()) {
                IPCResponse response = processCommand(command);
                sendResponse(client_fd, response);
            }
        }
    }
    
    
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        client_fds_.erase(
            std::remove(client_fds_.begin(), client_fds_.end(), client_fd),
            client_fds_.end()
        );
    }
    
    
    {
        std::lock_guard<std::mutex> lock(subscriber_mutex_);
        subscribers_.erase(
            std::remove(subscribers_.begin(), subscribers_.end(), client_fd),
            subscribers_.end()
        );
    }
    
    close(client_fd);
}

IPCResponse IPCServer::processCommand(const std::string& command) {
    
    if (command.find("{") == 0) {
        return processJSONRPC(command);
    }
    
    
    auto args = parseCommand(command);
    
    if (args.empty()) {
        return IPCResponse::error("Empty command");
    }
    
    const std::string& cmd = args[0];
    return processLegacyCommand(cmd, args);
}





IPCResponse IPCServer::processJSONRPC(const std::string& json) {
    
    
    
    
    size_t method_start = json.find("\"method\"");
    if (method_start == std::string::npos) {
        return IPCResponse::error("Invalid JSON-RPC: missing method");
    }
    
    size_t method_colon = json.find(":", method_start);
    if (method_colon == std::string::npos) {
        return IPCResponse::error("Invalid JSON-RPC: malformed method");
    }
    
    size_t method_quote_start = json.find("\"", method_colon + 1);
    size_t method_quote_end = json.find("\"", method_quote_start + 1);
    if (method_quote_start == std::string::npos || method_quote_end == std::string::npos) {
        return IPCResponse::error("Invalid JSON-RPC: method must be string");
    }
    
    std::string method = json.substr(method_quote_start + 1, method_quote_end - method_quote_start - 1);
    
    
    std::vector<std::string> params;
    size_t params_start = json.find("\"params\"");
    if (params_start != std::string::npos) {
        size_t params_bracket = json.find("[", params_start);
        size_t params_end = json.find("]", params_bracket);
        if (params_bracket != std::string::npos && params_end != std::string::npos) {
            std::string params_str = json.substr(params_bracket + 1, params_end - params_bracket - 1);
            size_t pos = 0;
            while (pos < params_str.size()) {
                size_t next_comma = params_str.find(",", pos);
                std::string param = params_str.substr(pos, next_comma - pos);
                param.erase(remove_if(param.begin(), param.end(), ::isspace), param.end());
                if (param.front() == '"' && param.back() == '"') {
                    param = param.substr(1, param.size() - 2);
                }
                if (!param.empty()) {
                    params.push_back(param);
                }
                if (next_comma == std::string::npos) break;
                pos = next_comma + 1;
            }
        }
    }
    
    
    return processLegacyCommand(method, params);
}

IPCResponse IPCServer::processLegacyCommand(const std::string& cmd, const std::vector<std::string>& args) {
    try {
        if (cmd == "workspaces" || cmd == "workspace") {
            return IPCResponse::ok("Workspaces retrieved", getWorkspacesJSON());
        }
        else if (cmd == "focused" || cmd == "focus") {
            return IPCResponse::ok("Focused window", 
                "{ \"window_id\": " + std::to_string(0) + " }");
        }
        else if (cmd == "window") {
            if (args.size() < 2) {
                return IPCResponse::error("Usage: window <window_id>");
            }
            Window w = static_cast<Window>(std::stoll(args[1]));
            return IPCResponse::ok("Window info", getWindowInfoJSON(w));
        }
        else if (cmd == "layout") {
            return IPCResponse::ok("Layout mode", getLayoutModeJSON());
        }
        else if (cmd == "subscribe") {
            return IPCResponse::ok("Subscribed", "{ \"subscribed\": true }");
        }
        else if (cmd == "unsubscribe") {
            return IPCResponse::ok("Unsubscribed", "{ \"subscribed\": false }");
        }
        else if (cmd == "reload" || cmd == "restart") {
            return IPCResponse::ok("Command sent", "{ \"action\": \"reload\" }");
        }
        else if (cmd == "quit" || cmd == "exit") {
            return IPCResponse::ok("Command sent", "{ \"action\": \"quit\" }");
        }
        else if (cmd == "help") {
            std::string help = R"({
                "jsonrpc": "2.0",
                "commands": [
                    {"name": "workspace", "desc": "Get workspace list", "params": []},
                    {"name": "focused", "desc": "Get focused window", "params": []},
                    {"name": "window", "desc": "Get window info", "params": ["window_id"]},
                    {"name": "layout", "desc": "Get current layout", "params": []},
                    {"name": "subscribe", "desc": "Subscribe to events", "params": []},
                    {"name": "reload", "desc": "Reload configuration", "params": []},
                    {"name": "quit", "desc": "Exit window manager", "params": []},
                    {"name": "help", "desc": "Show this help", "params": []}
                ]
            })";
            return IPCResponse::ok("Help", help);
        }
        else {
            if (command_callback_) {
                command_callback_(cmd, std::vector<std::string>(args.begin() + 1, args.end()));
                return IPCResponse::ok("Command executed");
            }
            return IPCResponse::error("Unknown command: " + cmd);
        }
    }
    catch (const std::exception& e) {
        return IPCResponse::error(std::string("Error: ") + e.what());
    }
}

std::string IPCServer::getWorkspacesJSON() const {
    return R"({"workspaces": []})";
}

std::string IPCServer::getWindowInfoJSON(Window w) const {
    std::ostringstream ss;
    ss << R"({"window_id": )" << w << R"(, "title": "", "class": "", "workspace": 0})";
    return ss.str();
}

std::string IPCServer::getLayoutModeJSON() const {
    return R"({"layout": "bsp"})";
}

bool IPCServer::sendResponse(int fd, const IPCResponse& response) {
    std::string output;
    
    if (response.success) {
        output = "OK|" + response.message + "|" + response.data + "\n";
    } else {
        output = "ERROR|" + response.message + "\n";
    }
    
    
    const char* ptr = output.c_str();
    size_t remaining = output.size();
    
    while (remaining > 0) {
        ssize_t sent = send(fd, ptr, remaining, 0);
        
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            return false;
        }
        
        if (sent == 0) {
            return false;
        }
        
        ptr += sent;
        remaining -= sent;
    }
    
    return true;
}

std::vector<std::string> IPCServer::parseCommand(const std::string& input) const {
    std::vector<std::string> args;
    std::istringstream iss(input);
    std::string arg;
    
    while (iss >> arg) {
        args.push_back(arg);
    }
    
    return args;
}

} 
