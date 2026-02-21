#pragma once

#include <X11/Xlib.h>
#include <cstdlib>
#include <sys/socket.h>
#include <sys/un.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace pblank {

static constexpr size_t MAX_IPC_CLIENTS = 32;

/**
 * @brief IPC command types that can be sent to Pointblank
 */
enum class IPCCommandType {
    GetWorkspaces,
    GetFocusedWindow,
    GetWindowInfo,
    GetLayoutMode,
    GetConfig,
    RunCommand,
    Subscribe,
    Unsubscribe
};

struct IPCResponse {
    bool success;
    std::string message;
    std::string data;  
    
    static IPCResponse ok(const std::string& msg = "", const std::string& json = "") {
        return {true, msg, json};
    }
    
    static IPCResponse error(const std::string& msg) {
        return {false, msg, ""};
    }
};

struct IPCCommand {
    IPCCommandType type;
    std::vector<std::string> args;
    int client_fd;
};

using IPCCallback = std::function<void(const std::string& command, const std::vector<std::string>& args)>;

class IPCServer {
public:
    IPCServer(Display* display, Window root);
    ~IPCServer();
    
    IPCServer(const IPCServer&) = delete;
    IPCServer& operator=(const IPCServer&) = delete;
    
    bool start();
    
    void stop();
    
    void broadcast(const std::string& message);
    
    void setCommandCallback(IPCCallback callback);
    
    bool isRunning() const { return running_.load(); }
    
    const std::string& getSocketPath() const { return socket_path_; }

private:
    Display* display_;
    Window root_;
    std::string socket_path_;
    int server_fd_;
    std::atomic<bool> running_;
    std::thread accept_thread_;
    std::vector<int> client_fds_;
    std::mutex client_mutex_;
    IPCCallback command_callback_;
    
    std::vector<int> subscribers_;
    std::mutex subscriber_mutex_;
    
    void acceptLoop();
    void handleClient(int client_fd);
    IPCResponse processCommand(const std::string& command);
    IPCResponse processJSONRPC(const std::string& json);
    IPCResponse processLegacyCommand(const std::string& cmd, const std::vector<std::string>& args);
    std::string getWorkspacesJSON() const;
    std::string getWindowInfoJSON(Window w) const;
    std::string getLayoutModeJSON() const;
    
    bool sendResponse(int fd, const IPCResponse& response);
    std::vector<std::string> parseCommand(const std::string& input) const;
};

inline IPCServer::IPCServer(Display* display, Window root)
    : display_(display)
    , root_(root)
    , server_fd_(-1)
    , running_(false)
{
    
    const char* home = std::getenv("HOME");
    std::string config_dir = home ? std::string(home) + "/.config/pblank" : "/tmp/pblank";
    socket_path_ = config_dir + "/pointblank.sock";
}

inline IPCServer::~IPCServer() {
    stop();
}

} 
