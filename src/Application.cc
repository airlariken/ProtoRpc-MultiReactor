
// ================= Implementation =================
#include "Application.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <unistd.h>

Application* Application::instance_ = nullptr;
std::mutex  Application::mutex_;

Application& Application::Instance(int argc, char** argv) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!instance_) {
        instance_ = new Application(argc, argv);
        atexit([](){ delete instance_; });
    }
    return *instance_;
}

Application::Application(int argc, char** argv)
        : argc_(argc), argv_(argv)
{
    ParseArgs();
    if (!config_path_.empty()) {
        LoadConfigFile(config_path_);
    }
}

void Application::ParseArgs() {
    int opt;
    while ((opt = getopt(argc_, argv_, "i:z:s:")) != -1) {
        switch (opt) {
            case 'i': config_path_   = optarg; break;
            case 'z': {
                std::string arg = optarg;
                auto pos = arg.find(':');
                if (pos != std::string::npos) {
                    zk_host_ = arg.substr(0, pos);
                    zk_port_ = std::stoi(arg.substr(pos+1));
                }
                break;
            }
            case 's': {
                std::string arg = optarg;
                auto pos = arg.find(':');
                if (pos != std::string::npos) {
                    server_host_ = arg.substr(0, pos);
                    server_port_ = std::stoi(arg.substr(pos+1));
                    std::cout<<"rpc server config is:"<<server_host_<<":"<<server_port_<<std::endl;
                }
                break;
            }
            default:
                std::cerr << "Usage: -i <config> -z <zk_host:port> -s <server_host:port>\n";
                std::exit(EXIT_FAILURE);
        }
    }
}

void Application::LoadConfigFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << path << std::endl;
        std::exit(EXIT_FAILURE);
    }

    std::string line;
    while (std::getline(file, line)) {
        Trim(line);
        if (line.empty() || line.front() == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        Trim(key);
        Trim(value);
        config_map_[key] = value;
    }
}

std::string Application::GetConfig(const std::string& key) const {
    auto it = config_map_.find(key);
    return it != config_map_.end() ? it->second : std::string();
}

std::string Application::ZkHost() const    { return zk_host_; }
int         Application::ZkPort() const    { return zk_port_; }
std::string Application::ServerHost() const{ return server_host_; }
int         Application::ServerPort() const{ return server_port_; }

void Application::Trim(std::string& str) {
    const char* whitespace = " \t\n\r";
    auto start = str.find_first_not_of(whitespace);
    auto end   = str.find_last_not_of(whitespace);
    if (start == std::string::npos) {
        str.clear();
    } else {
        str = str.substr(start, end - start + 1);
    }
}