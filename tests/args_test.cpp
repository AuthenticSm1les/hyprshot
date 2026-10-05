// Unit tests for command-line parsing.
#include <iostream>
#include <string>
#include <vector>

#include "args.hpp"
#include "check.hpp"

using namespace shot;

namespace {

ParseResult parse(std::vector<std::string> tokens) {
    std::vector<char*> argv;
    argv.reserve(tokens.size() + 1);
    argv.push_back(const_cast<char*>("shot"));
    for (auto& token : tokens)
        argv.push_back(token.data());
    return parse_args(static_cast<int>(argv.size()), argv.data());
}

bool is_error(const std::vector<std::string>& tokens) {
    return parse(tokens).status == ParseStatus::Error;
}

void test_defaults() {
    const ParseResult result = parse({"screen"});
    check::expect(result.status == ParseStatus::Ok, "screen parses");
    check::expect(result.args.mode == Mode::Screen, "screen mode");
    check::expect(!result.args.copy && !result.args.copy_only && !result.args.open_file,
                  "flags default to off");
    check::expect_eq(result.args.delay, 0.0, "delay defaults to 0");
    check::expect(result.args.dir.empty() && result.args.file.empty(), "paths default to empty");

    check::expect_eq(mode_name(parse({"window"}).args.mode), std::string("window"), "window mode");
    check::expect_eq(mode_name(parse({"section"}).args.mode), std::string("section"), "section mode");
    // Compare as std::string: expect_eq on two const char* would compare pointers.
    check::expect_eq(std::string(mode_name(Mode::Section)), std::string("section"), "mode_name");
}

void test_help_and_version() {
    check::expect(parse({"--help"}).status == ParseStatus::ShowHelp, "--help");
    check::expect(parse({"-h"}).status == ParseStatus::ShowHelp, "-h");
    check::expect(parse({"screen", "--help"}).status == ParseStatus::ShowHelp, "--help after a mode");
    check::expect(parse({"--help", "screen"}).status == ParseStatus::ShowHelp, "--help before a mode");
    check::expect(parse({"--version"}).status == ParseStatus::ShowVersion, "--version");
    check::expect(parse({"window", "--version"}).status == ParseStatus::ShowVersion,
                  "--version after a mode");
}

void test_options() {
    const ParseResult result =
        parse({"--copy", "section", "-d", "/tmp/pics", "-f", "shot.png", "--delay", "2.5", "-o"});

    check::expect(result.status == ParseStatus::Ok, "combined options parse");
    check::expect_eq(mode_name(result.args.mode), std::string("section"), "mode after leading option");
    check::expect(result.args.copy, "--copy");
    check::expect(!result.args.copy_only, "copy-only stays off");
    check::expect(result.args.open_file, "--open");
    check::expect_eq(result.args.dir, std::string("/tmp/pics"), "--dir value");
    check::expect_eq(result.args.file, std::string("shot.png"), "--file value");
    check::expect_eq(result.args.delay, 2.5, "--delay value");

    const ParseResult monitor = parse({"screen", "--monitor", "DP-1"});
    check::expect(monitor.status == ParseStatus::Ok, "monitor parses");
    check::expect_eq(monitor.args.monitor, std::string("DP-1"), "--monitor value");

    const ParseResult copy_only = parse({"--copy-only", "screen"});
    check::expect(copy_only.status == ParseStatus::Ok, "--copy-only parses");
    check::expect(copy_only.args.copy_only && !copy_only.args.copy, "copy-only is exclusive-by-flag");
}

void test_equals_syntax() {
    const ParseResult result = parse({"screen", "--delay=3", "--dir=/tmp/x", "--file=shot.png"});
    check::expect(result.status == ParseStatus::Ok, "--option=value parses");
    check::expect_eq(result.args.delay, 3.0, "--delay=value");
    check::expect_eq(result.args.dir, std::string("/tmp/x"), "--dir=value");
    check::expect_eq(result.args.file, std::string("shot.png"), "--file=value");
}

void test_errors() {
    check::expect(is_error({"bogus"}), "unknown mode is an error");
    check::expect(is_error({"focused"}), "the removed 'focused' mode is rejected");
    check::expect(is_error({"--nope", "screen"}), "unknown option is an error");
    check::expect(is_error({"--copy"}), "missing mode is an error");
    check::expect(is_error({"screen", "window"}), "two modes are an error");
    check::expect(is_error({"screen", "--dir"}), "missing option value is an error");
    check::expect(is_error({"screen", "--delay"}), "missing delay value is an error");

    // A malformed delay must be reported, not thrown.
    check::expect(is_error({"screen", "--delay", "abc"}), "junk delay is an error");
    check::expect(is_error({"screen", "--delay", "2s"}), "suffixed delay is an error");
    check::expect(is_error({"screen", "--delay", "-1"}), "negative delay is an error");
    check::expect(is_error({"screen", "--delay", "inf"}), "infinite delay is an error");
    check::expect(is_error({"screen", "--delay", ""}), "empty delay is an error");

    check::expect(is_error({"screen", "--copy", "--copy-only"}), "copy flags conflict");
    check::expect(is_error({"screen", "--copy-only", "--open"}), "--open with --copy-only conflicts");
    check::expect(is_error({"window", "--monitor", "DP-1"}), "--monitor outside screen mode conflicts");

    const ParseResult result = parse({"screen", "--delay", "abc"});
    check::expect(result.message.find("--delay") != std::string::npos, "error names the option");
}

void test_program_name() {
    check::expect_eq(program_name("/usr/local/bin/shot"), std::string("shot"), "strips directories");
    check::expect_eq(program_name("shot"), std::string("shot"), "bare name");
    check::expect_eq(program_name(nullptr), std::string("shot"), "null falls back");
}

}  // namespace

int main() {
    test_defaults();
    test_help_and_version();
    test_options();
    test_equals_syntax();
    test_errors();
    test_program_name();
    return check::report("args_test");
}
