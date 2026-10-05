// Running external commands safely.
//
// The helpers here exist because the obvious implementation
// (write all of stdin, then read stdout, then read stderr) deadlocks as soon
// as the child fills a pipe buffer, and because writing to a child that has
// already exited raises SIGPIPE and kills the whole process.
#pragma once

#include <string>
#include <vector>

namespace shot {

struct CommandResult {
    int         exit_code   = -1;  // exit status, or -1 if it never exited normally
    int         term_signal = 0;   // signal number if the process was killed
    std::string out;               // captured stdout
    std::string err;               // captured stderr

    [[nodiscard]] bool ok() const { return term_signal == 0 && exit_code == 0; }

    // Human-readable reason the command failed; empty when it succeeded.
    [[nodiscard]] std::string error_text() const;
};

struct PipelineResult {
    int         producer_code = -1;
    int         consumer_code = -1;
    std::string producer_err;
    std::string consumer_err;

    [[nodiscard]] bool ok() const { return producer_code == 0 && consumer_code == 0; }
    [[nodiscard]] std::string error_text() const;
};

// Whether `program` can be executed (a PATH lookup; no shell is involved).
bool command_exists(const std::string& program);

// Run `argv` to completion, feeding `input` to its stdin and capturing both
// output streams. Multiplexes all three pipes, so it cannot deadlock, and a
// child that exits early is reported instead of raising SIGPIPE in us.
CommandResult run(const std::vector<std::string>& argv, const std::string& input = {});

// Connect the stdout of `producer` to the stdin of `consumer`, run both, and
// capture their stderr. Used so a capture can be piped straight into the
// clipboard without an intermediate file.
PipelineResult run_pipeline(const std::vector<std::string>& producer,
                            const std::vector<std::string>& consumer);

// Start `argv` in a new session with stdio detached from this process, and
// return without waiting. The caller must not depend on the child's result.
bool spawn_detached(const std::vector<std::string>& argv);

}  // namespace shot
