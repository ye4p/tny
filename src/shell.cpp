#include "../include/shell.hpp"

void Shell::loop() {
    int status;

    do {
        std::cout << '>';

        std::string line;
        Lexer lexer;
        Parser parser;

        do {
            std::getline(std::cin, line);

            lexer.point_to(&line);
            auto [ps, tokens] = lexer.tokenize();

            for (const Token& t : tokens) {
                std::cout<< "Token: <" << t.content << "> of type " << static_cast<int>(t.tt) << '\n';
            }

            parser.point_to(&tokens);
            parser.cleanup();

            if (ps != ParseStatus::KeepParsing)
                break;
        } while (true);

        cs = parser.parse();
        std::cout << cs;

        status = execute_all_commands();
    } while (status);
}

std::tuple<int, std::string> Shell::launch(const std::vector<std::string>& args, FileDescriptor fd) {
    pid_t pid; // Removed wpid cause its unnecessary at the moment

    int out_pipe[2];
    int err_pipe[2];

    if (pipe(out_pipe) == -1)
        throw std::runtime_error("stdout pipe failed");

    if (pipe(err_pipe) == -1) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        throw std::runtime_error("stderr pipe failed");
    }

    pid = fork();

    if (pid == 0) {
        // Child process:
        // Construct a vector of char*:
        std::vector<char*> argv_storage = get_argv(args);
        char* const* argv = argv_storage.data();

        close(out_pipe[0]);
        close(err_pipe[0]);

        if (dup2(out_pipe[1], STDOUT_FILENO) == -1 || dup2(err_pipe[1], STDERR_FILENO) == -1) {
            _exit(127);
        }

        close(out_pipe[1]);
        close(out_pipe[1]);

        if (execvp(argv[0], argv) == -1)
            perror("tny");
        std::exit(EXIT_FAILURE);
    } else if (pid < 0) {
        // Error forking:
        perror("tny");
    } else {
        // Parent process:

        close(out_pipe[1]);
        close(err_pipe[1]);

        std::string stdout_data;
        std::string stderr_data;

        bool out_open = true;
        bool err_open = true;

        char buffer[4096];

        while (out_open || err_open) {
            fd_set fds;
            FD_ZERO(&fds);

            int maxfd = -1;

            if (out_open) {
                FD_SET(out_pipe[0], &fds);
                maxfd = out_pipe[0];
            }

            if (err_open) {
                FD_SET(err_pipe[0], &fds);
                if (err_pipe[0] > maxfd)
                    maxfd = err_pipe[0];
            }

            int ready = select(maxfd + 1, &fds, nullptr, nullptr, nullptr);

            if (ready == -1) {
                if (errno == EINTR) continue;
                perror("select");
                break;
            }

            if (out_open && FD_ISSET(out_pipe[0], &fds)) {
                ssize_t n = read(out_pipe[0], buffer, sizeof(buffer));

                if (n > 0) 
                //    std::cout.write(buffer, n);
                    stdout_data.append(buffer, n);
                else if (n == 0) {
                    close(out_pipe[0]);
                    out_open = false;
                }
            }

            if (err_open && FD_ISSET(err_pipe[0], &fds)) {
                ssize_t n = read(err_pipe[0], buffer, sizeof(buffer));

                if (n > 0) 
                //    std::cerr.write(buffer, n);
                    stderr_data.append(buffer, n);
                else if (n == 0) {
                    close(err_pipe[0]);
                    err_open = false;
                }
            }
        }

        if (out_open) close(out_pipe[0]);
        if (err_open) close(err_pipe[0]);

        std::string total;
        if (fd == FileDescriptor::StandardOutput || fd == FileDescriptor::Combined) {
            total += stdout_data;
            std::cout << "stdout_data: " << stdout_data << '\n';
        }
        if (fd == FileDescriptor::StandardError || fd == FileDescriptor::Combined) {
            total += stderr_data;
            std::cout << "stderr_data: " << stderr_data << '\n';
        }

        int status;
        if (waitpid(pid, &status, 0) == -1) {
            perror("waitpid");
        } else if (WIFEXITED(status)) {
            int exit_code = WEXITSTATUS(status); // 0 success, other - error
            return {!exit_code, total};
        } else  if (WIFSIGNALED(status)) {
            // int term_sig = WTERMSIG(status);
        }

        return {1, total};
    }

    return {1, ""};
}

std::tuple<int, std::string> Shell::execute(const std::vector<std::string>& args, FileDescriptor fd) {
    if (args.empty())
        return {1, ""}; // Since no command was entered.

    std::cout << "trying to execute command "<< args.at(0) << " and the following arguments: \n";
    for (size_t i = 1; i < args.size(); ++i) {
        std::cout << args[i] << '\n';
    }
    std::cout << "File descriptor chosen is " << static_cast<int>(fd) << '\n';

    for (size_t i = 0; i < num_builtins(); ++i) {
        if (args[0] == builtin_str_[i]) {
            return call_builtin(args, i, fd);
        }
    }

    return launch(args, fd);
}

int Shell::execute_all_commands() {
    int status = 1;

    // In the future can add support of the background operator.
    
    for (size_t i = 0; i < cs.vec.size(); ++i) {
        status = execute_logical_expression(cs.vec[i]);
    }

    return status;
}

int Shell::execute_logical_expression(LogicalExpression& le) {
    if (le.ops.size() + 1 != le.pipelines.size()) {
        throw std::runtime_error("Number of expressions and logical operators doesn't match\n");
    }

    if (le.ops.empty()) {
        return execute_pipeline(le.pipelines[0]);
    }

    size_t i = 0;
    int status;
    while (i < le.pipelines.size()) {
        status = execute_pipeline(le.pipelines[i]);
        if (status) { // That short circuits all logical OR
            while (i < le.ops.size() && le.ops[i] == LogicalOperator::Or) {
                ++i;
            }
            ++i;
        } else {
            while (i < le.ops.size() && le.ops[i] == LogicalOperator::And) {
                ++i;
            }
            ++i;
        }
    }

    return status;
}

int Shell::execute_pipeline(Pipeline& p) {
    // For now ignore pipeline type
    std::string prev;
    int status = 1;

    for (size_t i = 0; i < p.commands.size(); ++i) {
        if (!prev.empty()) {
            // p.commands[i].clear();
            p.commands[i].args.emplace_back(Word{
                .word = prev,
                .quote = false,
            });
            prev.clear();
        }
        auto [temp, out] = execute_command(p.commands[i], FileDescriptor::Combined);
        status = temp;
        prev = out;
    }

    // std::cout << prev;

    return status;
}

std::tuple<int, std::string> Shell::execute_command(Command& c, FileDescriptor fd) {
    std::string in;

    // Handle input redirections/proces substitutions
    if (!c.ps.empty() && std::find_if(c.ps.begin(), c.ps.end(), [](const ProcessSubstitution& ps) {
            return ps.st == SubstitutionType::Input;
                }) != c.ps.end()) {
        // Handle process substitution.
        for (size_t i = 0; i < c.ps.size(); ++i) {
            if (c.ps[i].st == SubstitutionType::Input) {
                auto [status, temp] = execute(get_args(c.ps[i].args), FileDescriptor::StandardOutput); // need to define function for conversion
                in = temp;
                break;
            }
        }
    } else {
        for (size_t i = 0; i < c.redirections.size(); ++i) {
            if (c.redirections[i].type == RedirectionType::Input) {
                // Now we need to read from the file
                auto [status, temp] = read_from_file(c.redirections[i].word.word);
                in = temp;
                break;
            }
        }
    }

    // Now execution of the command itself.
    std::vector<std::string> temp = expand_globs(c.args);

    if (!in.empty())
        temp.push_back(in);
    auto [status, output] = execute(temp, fd);

    bool output_redirected = false;
    // If output redirection/ process substitution exists, pass there, and return nothing.
    if (!c.redirections.empty()) {
        for (size_t i = 0; i < c.redirections.size(); ++i) {
            if (c.redirections[i].type == RedirectionType::Output) {
                int status = write_into_file(c.redirections[i].word.word, output);
                output_redirected = true;
                (void)status;
                break;
            }
        }
    } else {
        for (size_t i = 0; i < c.ps.size(); ++i) {
            if (c.ps[i].st == SubstitutionType::Output) {
                c.ps[i].args.push_back(Word{
                        .word = output,
                        .quote = false,
                        });
                output_redirected = true;
                return execute(get_args(c.ps[i].args), FileDescriptor::None); // Right now ignores all the other types of redirection
            }
        }
    }

    if (!output_redirected) {
        std::cout << "SHELL OUTPUT: ";
        std::cout << output;
    }

    return {status, output};
}

std::vector<char*> Shell::get_argv(const std::vector<std::string>& args) {
    std::vector<char*> temp;
    for (const auto& s : args) {
        temp.push_back(const_cast<char*>(s.data()));
    }
    temp.push_back(nullptr);

    return temp;
}

std::vector<char*> Shell::get_argv(const std::vector<Word>& args) {
    std::vector<char*> temp;
    for (const auto& s : args) {
        temp.push_back(const_cast<char*>(s.word.data()));
    }
    temp.push_back(nullptr);

    return temp;
}


std::vector<std::string> Shell::get_args(const std::vector<Word>& args) {
    std::vector<std::string> res;
    for (const Word& w: args) {
        res.push_back(w.word);
    }

    return res;
}

void Shell::print_and_append(std::ostream& out, std::string& str, const std::string& text, FileDescriptor fd) {
    out << text;
    if (fd == FileDescriptor::Combined || (fd == FileDescriptor::StandardOutput && &out == &std::cout) || (fd == FileDescriptor::StandardError && &out == &std::cerr))
        str += text;
}

std::tuple<int, std::string> Shell::read_from_file(const std::string& path) {
    std::ifstream in(path);

    if (!in.is_open()) {
        return {0, ""};
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();

    return {1, buffer.str()};
}

int Shell::write_into_file(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::app);

    if (!out.is_open()) {
        return 0;
    }

    out << content;
    return 1;
}



size_t Shell::num_builtins() {
    return builtin_func_.size();
}

std::tuple<int, std::string> Shell::call_builtin(const std::vector<std::string>& args, size_t i, FileDescriptor fd) {
    return (this->*builtin_func_[i])(args, fd);
}

//
// Builtin functions definitions
//


std::tuple<int, std::string> Shell::cd(const std::vector<std::string>& args, FileDescriptor fd) {
    std::string total;

    if (args.size() <= 1) {
        const char* err_txt = "tny: expected argument to \"cd\"\n";
        print_and_append(std::cerr, total, err_txt, fd);
    } else {
        if (chdir(args.at(1).c_str()) != 0) {
            perror("tny");
            if (fd == FileDescriptor::StandardError) 
                total += "tny";
        }
    }

    return {1, total};
}

std::tuple<int, std::string> Shell::help([[maybe_unused]] const std::vector<std::string>& args, FileDescriptor fd) {
    std::string total;
    const char* main_msg = "DG's tny shell\nType command with its arguments and click enter\nBuilt in programs:\n";
    print_and_append(std::cout, total, main_msg, fd);

    for (const std::string& s : builtin_str_) {
        const std::string temp = "  " + s + '\n';
        print_and_append(std::cout, total, temp, fd);
    }

    const char* man_msg = "use man page to find more information about commands\n";
    print_and_append(std::cout, total, man_msg, fd);

    return {1, total};
}

std::tuple<int, std::string> Shell::exit([[maybe_unused]] const std::vector<std::string>& args, [[maybe_unused]] FileDescriptor fd) {
    return {0, ""};
}

std::tuple<int, std::string> Shell::pwd([[maybe_unused]] const std::vector<std::string>& args, FileDescriptor fd) {
    std::string total;

    std::string path_msg = std::filesystem::current_path().string() + '\n';
    print_and_append(std::cout, total, path_msg, fd);

    return {1, total};
}

std::tuple<int, std::string> Shell::type(const std::vector<std::string>& args, FileDescriptor fd) {
    std::string total;

    if (args.size() <= 1) {
        const char* err_msg = "tny: expected argument to \"type\"\n";
        print_and_append(std::cerr, total, err_msg, fd);
    }
    if (std::find(builtin_str_.begin(), builtin_str_.end(), args[1]) != builtin_str_.end()) {
        const char* msg1 = "Shell builtin\n";
        print_and_append(std::cout, total, msg1, fd);
    }
    else {
        const char* msg2 = "Not a shell builtin\n";
        print_and_append(std::cout, total, msg2, fd);
    }
    return {1, total};
}


//
// GLOB EXPANSION
//

std::vector<std::string> Shell::expand_globs(const std::vector<Word>& args) {
    std::vector<std::string> res;
    for (const Word& w : args) {
        if (w.quote) {
            res.push_back(w.word);
        } else {
            // glob_t obj{};
            // int stat = glob(w.word.data(), GLOB_NOCHECK, nullptr, &obj);
            // if (stat) {
            //     perror("glob expansion");
            //     return res;
            // }
            // for (char** current = obj.gl_pathv; *current != nullptr; ++current) {
            //     res.emplace_back(*current);
            // }

            // globfree(ptr); // This is the built in POSIX way of glob matching.

            std::filesystem::path current_dir = ".";
            std::vector<std::string> directory_objects;
            for (const auto& entry : std::filesystem::directory_iterator(current_dir)) {
                directory_objects.emplace_back(
                    entry.path().filename().string()
                );
            }

            const std::string& temp = w.word;
            bool matched = false;
            for (const std::string& obj : directory_objects) {
                if (glob_match(temp, obj)) {
                    res.push_back(obj);
                    matched = true;
                }
            }

            if (!matched) {
                res.push_back(temp);
            }
        }
    }
    return res;
}

bool Shell::glob_match(std::string_view pattern, std::string_view str) {
    size_t p_idx = 0;
    size_t s_idx = 0;
    size_t star_idx = std::string_view::npos;
    size_t match_idx = 0;

    while (s_idx < str.size()) {
        // ?
        if (p_idx < pattern.size() && (pattern[p_idx] == '?' || pattern[p_idx] == str[s_idx])) {
            ++p_idx;
            ++s_idx;
        }

        // *
        else if (p_idx < pattern.size() && pattern[p_idx] == '*') {
            star_idx = p_idx;
            match_idx = s_idx;
            ++p_idx;
        }

        else if (star_idx != std::string_view::npos) {
            p_idx = star_idx + 1;
            ++match_idx;
            s_idx = match_idx;
        }

        //mismatch
        else {
            return false;
        }
    }

    // Remaining * characters
    while (p_idx < pattern.size() && pattern[p_idx] == '*') {
        ++p_idx;
    }

    return p_idx == pattern.size();
}

