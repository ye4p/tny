#pragma once

#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include <fstream>

#include "parser.hpp"
#include "node.hpp"

enum class FileDescriptor {
    StandardInput = 0,
    StandardOutput = 1,
    StandardError = 2,
    Combined = 3,
};

class Shell {
private:
    // With new parser, we might not need to store args here as each command already stores its own arguments
    // std::vector<std::string> args_;

    CommandSeparator cs;

    std::vector<std::string> builtin_str_ = {
        "cd",
        "help",
        "exit",
        "pwd",
        "type",
    };

    using func = std::tuple<int, std::string> (Shell::*)(const std::vector<std::string&> args, FileDescriptor fd);

    // int (*builtin_func[])() = {
    std::array<func, 5> builtin_func_ {
        &Shell::cd,
        &Shell::help,
        &Shell::exit,
        &Shell::pwd,
        &Shell::type,
    };

public:
    void loop();
    std::string read_line() const;
    void split_line(std::string& line);

    std::tuple<int, std::string> launch(const std::vector<std::string>& args, FileDescriptor fd);
    std::tuple<int, std::string> execute(const std::vector<std::string>& args, FileDescriptor fd); // The main one - call it first

    int execute_all_commands();
    int execute_logical_expression(const LogicalExpression& le);
    int execute_pipeline(const Pipeline& p);
    std::tuple<int, std::string> execute_command(const Command& c, FileDescriptor fd); 

    size_t num_builtins();
    std::tuple<int, std::string> call_builtin(const std::vector<std::string>& args, size_t i, FileDescriptor fd);

    // Helper methods:
    std::vector<const char*> get_argv(const std::vector<std::string>& args);
    std::vector<const char*> get_argv(const std::vector<Word>& args);
    std::vector<std::string> get_args(const std::vector<Word>& args);
    void print_and_append(std::ostream& out, std::string& str, const std::string& text, FileDescriptor fd);
    std::tuple<int, std::string> read_from_file(const std::string& path);

    // Builtin commands:
    std::tuple<int, std::string> cd(const std::vector<std::string>& args, FileDescriptor fd);
    std::tuple<int, std::string> help(const std::vector<std::string>& args, FileDescriptor fd);
    std::tuple<int, std::string> exit(const std::vector<std::string>& args, FileDescriptor fd);
    std::tuple<int, std::string> pwd(const std::vector<std::string>& args, FileDescriptor fd);
    std::tuple<int, std::string> type(const std::vector<std::string>& args, FileDescriptor fd);

};
