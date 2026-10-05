#include "process.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <optional>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace shot {
namespace {

constexpr std::size_t READ_CHUNK  = 64 * 1024;
constexpr int         EXEC_FAILED = 127;
constexpr int         NO_FD       = -1;

// How long to wait for pipe activity before checking whether the child has
// exited. Only reached when no pipe is readable or writable.
constexpr int POLL_TIMEOUT_MS = 100;

void close_fd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

void make_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

bool is_executable_file(const std::string& path) {
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        return false;
    return ::access(path.c_str(), X_OK) == 0;
}

std::vector<char*> argv_of(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args)
        argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    return argv;
}

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string describe_status(int exit_code, int term_signal) {
    if (term_signal != 0)
        return std::string("killed by signal ") + std::to_string(term_signal);
    return "exit status " + std::to_string(exit_code);
}

// Replace this process image. A fd of NO_FD means "leave the inherited one alone".
[[noreturn]] void exec_child(const std::vector<std::string>& args, int stdin_fd, int stdout_fd,
                             int stderr_fd, const std::vector<int>& parent_fds) {
    if (stdin_fd != NO_FD)
        ::dup2(stdin_fd, STDIN_FILENO);
    if (stdout_fd != NO_FD)
        ::dup2(stdout_fd, STDOUT_FILENO);
    if (stderr_fd != NO_FD)
        ::dup2(stderr_fd, STDERR_FILENO);

    for (const int fd : parent_fds) {
        if (fd > STDERR_FILENO && fd != stdin_fd && fd != stdout_fd && fd != stderr_fd)
            ::close(fd);
    }

    auto argv = argv_of(args);
    ::execvp(argv[0], argv.data());
    ::_exit(EXEC_FAILED);
}

int wait_for(pid_t pid) {
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return -1;
    }
    return status;
}

// Decode a wait status into the result fields.
void record_status(CommandResult& result, int status) {
    if (status < 0) {
        result.exit_code   = -1;
        result.term_signal = 0;
        return;
    }
    if (WIFEXITED(status)) {
        result.exit_code   = WEXITSTATUS(status);
        result.term_signal = 0;
    } else if (WIFSIGNALED(status)) {
        result.exit_code   = -1;
        result.term_signal = WTERMSIG(status);
    }
}

// Read everything currently available, appending to `sink`.
// Returns false once the fd reports end-of-file or a hard error.
bool drain(int fd, std::string& sink) {
    char buffer[READ_CHUNK];
    for (;;) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            sink.append(buffer, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0)
            return false;
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return true;
        return false;
    }
}

// Locate `program` on PATH. A name containing a slash is used as-is.
std::optional<std::string> find_in_path(const std::string& program) {
    if (program.empty())
        return std::nullopt;

    if (program.find('/') != std::string::npos)
        return is_executable_file(program) ? std::optional{program} : std::nullopt;

    const char*       path_env = ::getenv("PATH");
    const std::string paths    = path_env != nullptr ? path_env : "/usr/local/bin:/usr/bin:/bin";

    std::size_t begin = 0;
    while (begin <= paths.size()) {
        const std::size_t end = paths.find(':', begin);
        std::string       dir = paths.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (dir.empty())
            dir = ".";
        else if (dir.back() != '/')
            dir.push_back('/');

        const std::string candidate = dir + program;
        if (is_executable_file(candidate))
            return candidate;

        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    return std::nullopt;
}

}  // namespace

std::string CommandResult::error_text() const {
    const std::string detail = trim(err);
    if (!detail.empty())
        return detail;
    if (ok())
        return {};
    return describe_status(exit_code, term_signal);
}

std::string PipelineResult::error_text() const {
    const std::string producer_detail = trim(producer_err);
    const std::string consumer_detail = trim(consumer_err);

    if (producer_code != 0)
        return producer_detail.empty() ? "capture failed: " + describe_status(producer_code, 0)
                                       : producer_detail;
    if (consumer_code != 0)
        return consumer_detail.empty() ? "clipboard copy failed: " + describe_status(consumer_code, 0)
                                       : consumer_detail;
    return {};
}

bool command_exists(const std::string& program) { return find_in_path(program).has_value(); }

CommandResult run(const std::vector<std::string>& argv, const std::string& input) {
    CommandResult result;
    if (argv.empty()) {
        result.err = "empty command";
        return result;
    }

    int in_pipe[2]  = {NO_FD, NO_FD};
    int out_pipe[2] = {NO_FD, NO_FD};
    int err_pipe[2] = {NO_FD, NO_FD};

    const auto fail = [&](const char* what) {
        result.err = std::string(what) + ": " + std::strerror(errno);
        close_fd(in_pipe[0]);
        close_fd(in_pipe[1]);
        close_fd(out_pipe[0]);
        close_fd(out_pipe[1]);
        close_fd(err_pipe[0]);
        close_fd(err_pipe[1]);
        return result;
    };

    if (::pipe(in_pipe) != 0 || ::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0)
        return fail("pipe");

    const pid_t pid = ::fork();
    if (pid < 0)
        return fail("fork");

    if (pid == 0) {
        exec_child(argv, in_pipe[0], out_pipe[1], err_pipe[1],
                   {in_pipe[0], in_pipe[1], out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1]});
    }

    close_fd(in_pipe[0]);
    close_fd(out_pipe[1]);
    close_fd(err_pipe[1]);

    make_nonblocking(out_pipe[0]);
    make_nonblocking(err_pipe[0]);
    make_nonblocking(in_pipe[1]);

    if (input.empty())
        close_fd(in_pipe[1]);

    std::size_t written = 0;
    int         status  = 0;
    bool        reaped  = false;

    for (;;) {
        std::array<pollfd, 3> poll_fds {};
        int                   in_slot = NO_FD, out_slot = NO_FD, err_slot = NO_FD, count = 0;

        if (in_pipe[1] >= 0)
            poll_fds[in_slot = count++] = {in_pipe[1], POLLOUT, 0};
        if (out_pipe[0] >= 0)
            poll_fds[out_slot = count++] = {out_pipe[0], POLLIN, 0};
        if (err_pipe[0] >= 0)
            poll_fds[err_slot = count++] = {err_pipe[0], POLLIN, 0};

        if (count == 0)
            break;

        const int ready = ::poll(poll_fds.data(), static_cast<nfds_t>(count), POLL_TIMEOUT_MS);
        if (ready < 0 && errno != EINTR)
            break;

        if (ready > 0) {
            if (in_slot != NO_FD) {
                const short revents = poll_fds[in_slot].revents;
                if (revents & POLLOUT) {
                    const ssize_t n =
                        ::write(in_pipe[1], input.data() + written, input.size() - written);
                    if (n > 0)
                        written += static_cast<std::size_t>(n);
                    else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                        close_fd(in_pipe[1]);
                }
                // A closed read end means the child is gone; stop feeding it.
                if (revents & (POLLERR | POLLHUP | POLLNVAL))
                    close_fd(in_pipe[1]);
                if (in_pipe[1] >= 0 && written >= input.size())
                    close_fd(in_pipe[1]);
            }

            if (out_slot != NO_FD && (poll_fds[out_slot].revents & (POLLIN | POLLHUP | POLLERR)))
                if (!drain(out_pipe[0], result.out))
                    close_fd(out_pipe[0]);

            if (err_slot != NO_FD && (poll_fds[err_slot].revents & (POLLIN | POLLHUP | POLLERR)))
                if (!drain(err_pipe[0], result.err))
                    close_fd(err_pipe[0]);
        }

        // Stop as soon as the child is gone. Waiting for end-of-file is not
        // enough: a command that forks (wl-copy) hands the write end of the
        // pipe to a grandchild that outlives it, so the read end never closes.
        if (!reaped) {
            const pid_t finished = ::waitpid(pid, &status, WNOHANG);
            if (finished == pid || (finished < 0 && errno != EINTR))
                reaped = true;
        }
        if (reaped) {
            // Collect whatever the child had already written, then stop.
            if (out_pipe[0] >= 0) {
                drain(out_pipe[0], result.out);
                close_fd(out_pipe[0]);
            }
            if (err_pipe[0] >= 0) {
                drain(err_pipe[0], result.err);
                close_fd(err_pipe[0]);
            }
            close_fd(in_pipe[1]);
            break;
        }
    }

    close_fd(in_pipe[1]);
    close_fd(out_pipe[0]);
    close_fd(err_pipe[0]);

    if (!reaped)
        status = wait_for(pid);

    record_status(result, status);
    return result;
}

PipelineResult run_pipeline(const std::vector<std::string>& producer,
                            const std::vector<std::string>& consumer) {
    PipelineResult result;
    if (producer.empty() || consumer.empty()) {
        result.producer_err = "empty command";
        return result;
    }

    int data_pipe[2] = {NO_FD, NO_FD};
    int prod_err[2]  = {NO_FD, NO_FD};
    int cons_err[2]  = {NO_FD, NO_FD};

    const auto close_pipes = [&] {
        close_fd(data_pipe[0]);
        close_fd(data_pipe[1]);
        close_fd(prod_err[0]);
        close_fd(prod_err[1]);
        close_fd(cons_err[0]);
        close_fd(cons_err[1]);
    };

    if (::pipe(data_pipe) != 0 || ::pipe(prod_err) != 0 || ::pipe(cons_err) != 0) {
        result.producer_err = std::string("pipe: ") + std::strerror(errno);
        close_pipes();
        return result;
    }

    int devnull_read = ::open("/dev/null", O_RDONLY);
    if (devnull_read < 0) {
        result.producer_err = std::string("open /dev/null: ") + std::strerror(errno);
        close_pipes();
        return result;
    }

    int devnull_write = ::open("/dev/null", O_WRONLY);
    if (devnull_write < 0) {
        result.producer_err = std::string("open /dev/null: ") + std::strerror(errno);
        ::close(devnull_read);
        close_pipes();
        return result;
    }

    const std::vector<int> parent_fds = {data_pipe[0], data_pipe[1], prod_err[0], prod_err[1],
                                         cons_err[0], cons_err[1]};

    const pid_t producer_pid = ::fork();
    if (producer_pid < 0) {
        result.producer_err = std::string("fork: ") + std::strerror(errno);
        ::close(devnull_read);
        ::close(devnull_write);
        close_pipes();
        return result;
    }
    if (producer_pid == 0)
        exec_child(producer, devnull_read, data_pipe[1], prod_err[1], parent_fds);

    const pid_t consumer_pid = ::fork();
    if (consumer_pid < 0) {
        // Without a reader the producer would block forever on a full pipe, so
        // terminate it instead of orphaning it.
        result.consumer_err = std::string("fork: ") + std::strerror(errno);
        close_fd(data_pipe[1]);
        close_fd(prod_err[1]);
        close_fd(cons_err[1]);
        ::kill(producer_pid, SIGTERM);
        wait_for(producer_pid);
        result.producer_code = -1;
        ::close(devnull_read);
        ::close(devnull_write);
        close_pipes();
        return result;
    }
    if (consumer_pid == 0) {
        exec_child(consumer, data_pipe[0], devnull_write, cons_err[1], parent_fds);
    }

    close_fd(devnull_read);
    close_fd(devnull_write);
    close_fd(data_pipe[0]);
    close_fd(data_pipe[1]);
    close_fd(prod_err[1]);
    close_fd(cons_err[1]);

    // drain() uses EAGAIN to mean "nothing more right now"; without this a
    // second read on a blocking pipe would hang until the child wrote again.
    make_nonblocking(prod_err[0]);
    make_nonblocking(cons_err[0]);

    int  producer_status = 0;
    int  consumer_status = 0;
    bool producer_reaped = false;
    bool consumer_reaped = false;

    for (;;) {
        std::array<pollfd, 2> poll_fds {};
        int                   prod_slot = NO_FD, cons_slot = NO_FD, count = 0;

        if (prod_err[0] >= 0)
            poll_fds[prod_slot = count++] = {prod_err[0], POLLIN, 0};
        if (cons_err[0] >= 0)
            poll_fds[cons_slot = count++] = {cons_err[0], POLLIN, 0};

        if (count == 0)
            break;

        const int ready = ::poll(poll_fds.data(), static_cast<nfds_t>(count), POLL_TIMEOUT_MS);
        if (ready < 0 && errno != EINTR)
            break;

        if (ready > 0) {
            if (prod_slot != NO_FD && (poll_fds[prod_slot].revents & (POLLIN | POLLHUP | POLLERR)))
                if (!drain(prod_err[0], result.producer_err))
                    close_fd(prod_err[0]);

            if (cons_slot != NO_FD && (poll_fds[cons_slot].revents & (POLLIN | POLLHUP | POLLERR)))
                if (!drain(cons_err[0], result.consumer_err))
                    close_fd(cons_err[0]);
        }

        // As in run(): a consumer that forks (wl-copy) leaves the write end of
        // its pipe open in a grandchild, so end-of-file never arrives.
        if (!producer_reaped) {
            const pid_t finished = ::waitpid(producer_pid, &producer_status, WNOHANG);
            if (finished == producer_pid || (finished < 0 && errno != EINTR))
                producer_reaped = true;
        }
        if (!consumer_reaped) {
            const pid_t finished = ::waitpid(consumer_pid, &consumer_status, WNOHANG);
            if (finished == consumer_pid || (finished < 0 && errno != EINTR))
                consumer_reaped = true;
        }
        if (producer_reaped && consumer_reaped) {
            if (prod_err[0] >= 0) {
                drain(prod_err[0], result.producer_err);
                close_fd(prod_err[0]);
            }
            if (cons_err[0] >= 0) {
                drain(cons_err[0], result.consumer_err);
                close_fd(cons_err[0]);
            }
            break;
        }
    }

    close_fd(prod_err[0]);
    close_fd(cons_err[0]);

    if (!producer_reaped)
        producer_status = wait_for(producer_pid);
    if (!consumer_reaped)
        consumer_status = wait_for(consumer_pid);

    // Reduce a wait status to an exit code, or -1 when a signal killed it.
    const auto exit_code_of = [](int status) {
        CommandResult decoded;
        record_status(decoded, status);
        return decoded.term_signal != 0 ? -1 : decoded.exit_code;
    };

    result.producer_code = exit_code_of(producer_status);
    result.consumer_code = exit_code_of(consumer_status);

    result.producer_err = trim(result.producer_err);
    result.consumer_err = trim(result.consumer_err);
    return result;
}

bool spawn_detached(const std::vector<std::string>& argv) {
    if (argv.empty())
        return false;

    const pid_t pid = ::fork();
    if (pid < 0)
        return false;

    if (pid == 0) {
        ::setsid();
        const int devnull = ::open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            ::dup2(devnull, STDIN_FILENO);
            ::dup2(devnull, STDOUT_FILENO);
            ::dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO)
                ::close(devnull);
        }
        auto args = argv_of(argv);
        ::execvp(args[0], args.data());
        ::_exit(EXEC_FAILED);
    }

    // Reap politely if it already finished; otherwise leave it running. This
    // process exits shortly afterwards, so a brief zombie is harmless.
    int   status = 0;
    pid_t reaped = 0;
    do {
        reaped = ::waitpid(pid, &status, WNOHANG);
    } while (reaped < 0 && errno == EINTR);

    return true;
}

}  // namespace shot
