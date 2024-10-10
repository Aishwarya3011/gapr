#include "logger.h"
#include <ctime>
#include <iomanip>
#include <sstream>
#include <filesystem>

// Logger constructor
Logger::Logger() {
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
    std::string currentDateTime = oss.str();

    std::string filename = "log_" + currentDateTime + ".txt";
    logFile.open(filename, std::ios_base::app); // Open log file in append mode

    if (!logFile.is_open()) {
        std::cerr << "Failed to open log file for writing." << std::endl;
    }
}

// Get the singleton instance of Logger
Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

// Log a message with the current timestamp and context (such as file name)
void Logger::logMessage(const std::string& context, const std::string& message) {
    if (logFile.is_open()) {
        logFile << "[" << currentDateTime() << "] (" << context << ") " << message << std::endl;
    }
}

// Get the current date/time as a string
std::string Logger::currentDateTime() {
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// Destructor to close the log file
Logger::~Logger() {
    if (logFile.is_open()) {
        logFile.close();
    }
}
