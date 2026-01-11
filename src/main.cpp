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
#include <limits>
#include <queue>

struct keyValue
{
    std::string key;
    std::string value;
};

namespace fs = std::filesystem;
class MemDB
{
private:
    mutable std::shared_mutex mutex_;
    fs::path pathToNextId = "./config/config.txt";
    std::unordered_map<std::string, std::string> memTable;
    std::condition_variable cv;
    std::thread writerThread;
    std::queue<std::unordered_map<std::string, std::string>> writerQueue;
    std::mutex queueMutex;
    std::atomic<int> nextId;
    size_t threshold;
    fs::path dataDirectory;
    std::string DBName;
    bool stopWriting = false;
    const std::string directory = "./data";

    void processQueue()
    {
        while (true)
        {
            std::unordered_map<std::string, std::string> snapshot;
            {
                std::unique_lock<std::mutex> queueLock(queueMutex);
                cv.wait(queueLock, [this]
                        { return !writerQueue.empty() || stopWriting; });
                if (stopWriting && writerQueue.empty())
                {
                    break;
                }
                snapshot = std::move(writerQueue.front());
                writerQueue.pop();
            }
            writeToDisk(std::move(snapshot));
        }
    }
    void SetDetails(const fs::path &configPath)
    {
        std::ifstream file(configPath);
        std::string line;
        if (!file.is_open())
        {
            std::cerr << "Error opening config file" << std::endl;
            return;
        }
        auto clean = [](std::string s)
        {
            const std::string garbage = " \t\r\n;\"\'";
            size_t first = s.find_first_not_of(garbage);
            if (first == std::string::npos)
                return std::string("");
            size_t last = s.find_last_not_of(garbage);
            return s.substr(first, (last - first + 1));
        };
        while (std::getline(file, line))
        {
            size_t firstChar = line.find_first_not_of(" \t");
            if (firstChar == std::string::npos || line[firstChar] == '#')
                continue;
            size_t delimiterPos = line.find('=');
            if (delimiterPos != std::string::npos)
            {
                std::string key = clean(line.substr(0, delimiterPos));
                std::string value = clean(line.substr(delimiterPos + 1));
                try
                {
                    if (key == "NEXT_FILE_ID")
                    {
                        nextId = std::stoi(value);
                    }
                    else if (key == "WRITE_THRESHOLD")
                    {
                        threshold = std::stoull(value);
                    }
                    else if (key == "DATA_DIRECTORY")
                    {
                        dataDirectory = value;
                    }
                    else if (key == "DB_NAME")
                    {
                        DBName = value;
                    }
                }
                catch (const std::exception &e)
                {
                    std::cerr << "Error parsing value for " << key << ": " << e.what() << std::endl;
                }
            }
        }
    }
    void writeToDisk(std::unordered_map<std::string, std::string> snapshot)
    {
        if (!snapshot.empty())
        {
            static std::mutex io_mutex;

            std::unique_lock<std::mutex> io_lock(io_mutex);
            int currentPoint = nextId;
            fs::path fullpath = dataDirectory / ("data_" + std::to_string(currentPoint) + ".bin");
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
            nextId++;
        }
    }

    void addToQueue(std::unordered_map<std::string, std::string> &data)
    {
        {
            std::unique_lock<std::mutex> queueLock(queueMutex);
            writerQueue.push(std::move(data));
        }
        cv.notify_one();
    }

public:
    MemDB() : writerThread(&MemDB::processQueue, this)
    {
        SetDetails("./config/config.txt");
    }
    ~MemDB()
    {
        {
            std::lock_guard<std::mutex> queueLock(queueMutex);
            stopWriting = true;
        }
        cv.notify_one();
        if (writerThread.joinable())
        {
            writerThread.join();
        }
    }
    std::string getDBName(void)
    {
        return DBName;
    }

    int length(void)
    {
        return memTable.size();
    }
    void put(const std::string &key, const std::string &value)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        memTable[key] = value;
        if (memTable.size() >= threshold)
        {
            std::unordered_map<std::string, std::string> snapshot = std::move(memTable);
            memTable.clear();
            addToQueue(snapshot);
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
void start(MemDB &table)
{
    while (true)
    {
        std::cout << "db prompt >>> ";
        std::string command, key, value;
        std::getline(std::cin, command);
        command = toLower(command);
        if (command == "q" || command == "e")
        {
            std::cout << "Flushing Memory " << std::endl;
            break;
        }
        else if (command == "put")
        {
            std::cout << "Input key >> ";
            std::getline(std::cin, key);
            std::cout << "input Value >> ";
            std::getline(std::cin, value);
            table.put(key, value);
            std::string message = std::format("Saved Key \"{}\" With Value \"{}\"", key, value);
            std::cout << message << std::endl;
        }
        else if (command == "get")
        {
            std::cout << "Input Key >> ";
            std::getline(std::cin, key);
            std::string message = std::format("Value with Key \"{}\" doesn't exist", key);
            std::optional<std::string> value = table.get(key);
            std::cout << value.value_or(message) << std::endl;
        }
        else if (command == "length")
        {
            std::cout << "Memory has " << table.length() << " items" << std::endl;
        }
        else
        {
            std::cout << "Unknown command" << std::endl;
        }
    }
}
int main()
{
    MemDB table;
    std::cout << table.getDBName() << " Starting " << std::endl;
    start(table);
    return 0;
}