#ifndef _APPLICATION_H
#define _APPLICATION_H

#include <string>
#include <unordered_map>
#include <mutex>

// Combined application and configuration manager
class Application {
public:
    // Initialize singleton with command-line arguments
    static Application& Instance(int argc = 0, char** argv = nullptr);

    // Accessors for configuration values
    std::string GetConfig(const std::string& key) const;
    std::string ZkHost() const;
    int         ZkPort() const;
    std::string ServerHost() const;
    int         ServerPort() const;

private:
    Application(int argc, char** argv);
    ~Application() = default;
    Application(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(const Application&) = delete;

    void ParseArgs();
    void LoadConfigFile(const std::string& path);
    void Trim(std::string& str);

    static Application*      instance_;
    static std::mutex        mutex_;

    // command-line and file parameters
    int                      argc_;
    char**                   argv_;
    std::string              config_path_;
    std::string              zk_host_ = "127.0.0.1";
    int                      zk_port_ = 2181;
    std::string              server_host_ = "192.168.3.1";
    int                      server_port_ = 8000;

    std::unordered_map<std::string, std::string> config_map_;
};

#endif // _APPLICATION_H_
