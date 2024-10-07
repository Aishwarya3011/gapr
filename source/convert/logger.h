#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <fstream>
#include <string>

class Logger {
public:
    // Static method to get the singleton instance of the Logger.
    static Logger& instance();

    // Log a simple message to the file with the context (such as file name)
    void logMessage(const std::string& context, const std::string& message);

    // Destructor to close the file stream if open.
    ~Logger();

private:
    std::ofstream logFile;

    // Private constructor to prevent instantiation
    Logger();

    // Utility to get the current date/time as a string
    std::string currentDateTime();
};

#endif // LOGGER_H
