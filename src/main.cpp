#include <iostream>
#include <string>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <optional>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <format>
#include <algorithm>
#include <cctype>
#include <vector>

std::string DBNAME = "VELOXDB";

struct keyValue
{
    std::string key;
    std::string value;
};

namespace fs = std::filesystem;

int current(const std::string &directory)
{
    int count = 0;
    try
    {
        if (fs::create_directories(directory))
        {
            return count;
        }
        else
        {
            for (const auto &entry : fs::directory_iterator(directory))
            {
                if (fs::is_regular_file(entry))
                {
                    count++;
                }
            }
        }
    }
    catch (const fs::filesystem_error &e)
    {
        std::cerr << "Error " << e.what() << "/n";
    }
    return count;
}

class MemDB
{
private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> memTable;
    std::condition_variable cv;
    std::thread writer;
    const size_t threshold = 1024 * 1024;
    const std::string directory = "./data";
    void writeToDisk(std::unordered_map<std::string, std::string> snapshot)
    {
        if (!snapshot.empty())
        {
            static std::mutex io_mutex;

            std::unique_lock<std::mutex> io_lock(io_mutex);
            int currentPoint = current(directory);
            fs::path fullpath = fs::current_path() / "data" / ("data_" + std::to_string(currentPoint) + ".bin");
            std::thread::id current_thread_id = std::this_thread::get_id();
            std::cout << current_thread_id << " Writing to " << fullpath << std::endl;
            io_lock.unlock();
            std::ofstream out(fullpath, std::ios::binary);
            for (const auto &[key, value] : snapshot)
            {
                uint32_t k_size = key.size();
                uint32_t v_size = value.size();
                out.write(reinterpret_cast<char *>(&k_size), sizeof(k_size));
                out.write(key.data(), k_size);
                out.write(reinterpret_cast<char *>(&v_size), sizeof(v_size));
                out.write(value.data(), v_size);
            }
        }
    }

public:
    int length(void)
    {
        return memTable.size();
    }
    void breake(void)
    {
        writeToDisk(memTable);
    }
    void put(const std::string &key, const std::string &value)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        memTable[key] = value;
        // writeToDisk(directory);
        if (memTable.size() * 64 > threshold)
        {
            std::unordered_map<std::string, std::string> snapshot = std::move(memTable);
            memTable.clear();
            std::thread([this, snapshot = std::move(snapshot)]() mutable
                        { writeToDisk(std::move(snapshot)); })
                .detach();
        }
    }
    std::optional<std::string> get(const std::string &key)
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = memTable.find(key);
        if (it != memTable.end())
        {
            return it->second;
        }
        std::vector<fs::path> Files;
        std::string target = ".bin";
        if (fs::exists(directory) && fs::is_directory(directory))
        {
            for (const auto &entry : fs::directory_iterator(directory))
            {
                if (entry.is_regular_file() && entry.path().extension() == target)
                {
                    Files.push_back(entry.path());
                }
            }

            std::sort(Files.begin(), Files.end(), [](const fs::path &a, const fs::path &b)
                      { return fs::last_write_time(a) > fs::last_write_time(b); });
            for (const auto &file : Files)
            {
                std::ifstream File(file, std::ios::binary);
                while (File.peek() != EOF)
                {
                    uint32_t k_size, v_size;

                    File.read(reinterpret_cast<char *>(&k_size), sizeof(k_size));
                    std::string diskKey;
                    diskKey.resize(k_size);
                    File.read(diskKey.data(), k_size);
                    File.read(reinterpret_cast<char *>(&v_size), sizeof(v_size));
                    std::string diskValue;
                    diskValue.resize(v_size);
                    File.read(diskValue.data(), v_size);
                    if (diskKey == key)
                    {
                        return diskValue;
                    }
                }
            }
        }
        return std::nullopt;
    }
};

std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                   { return std::tolower(c); });
    return s;
}
int main()
{
    std::cout << DBNAME << " Starting " << std::endl;
    std::string line;
    MemDB table;
    while (true)
    {
        std::cout << "db prompt >>> ";
        if (!std::getline(std::cin, line))
            break;
        std::stringstream ss(line);
        std::string command;
        std::string key;
        std::string value;
        ss >> command;
        if (toLower(command) == "q" || toLower(command) == "e")
        {
            std::cout << "Flushing Memory " << std::endl;
            table.breake();
            break;
        }
        else if (toLower(command) == "put")
        {
            ss >> key;
            ss >> value;
            table.put(key, value);
            std::string message = std::format("Saved Key \"{}\" With Value \"{}\"", key, value);
            std::cout << message << std::endl;
        }
        else if (toLower(command) == "get")
        {
            ss >> key;
            std::string message = std::format("Value with Key \"{}\" doesn't exist", key);
            std::optional<std::string> value = table.get(key);
            std::cout << value.value_or(message) << std::endl;
        }
        else if (toLower(command) == "length")
        {
            std::cout << "Memory has " << table.length() << " items" << std::endl;
        }
        else
        {
            std::cout << "Unknown command" << std::endl;
        }
    }
    return 0;
}