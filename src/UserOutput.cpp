#include "common/UserOutput.h"
#include <iostream>
#include <unistd.h>
#include <chrono>
#include <ctime>

namespace common {

namespace {
    // ANSI escape codes
    const char* COLOR_RESET = "\033[0m";
    const char* COLOR_WHITE = "\033[37m";
    const char* COLOR_YELLOW = "\033[33m";   // для скобок и error
    const char* COLOR_GRAY = "\033[90m";     // для второстепенной информации
    const char* COLOR_BOLD = "\033[1m";
}

UserOutput::UserOutput(OutputLevel level)
    : level_(level)
    , useColors_(isatty(STDOUT_FILENO) != 0) {}

OutputLevel UserOutput::fromString(const std::string& str) {
    if (str == "none") return OutputLevel::None;
    if (str == "tools") return OutputLevel::Tools;
    if (str == "main") return OutputLevel::Main;
    if (str == "deep") return OutputLevel::Deep;
    return OutputLevel::Main; // default
}

void UserOutput::printLine(const std::string& line) {
    std::cout << line << "\n" << std::flush;
}

void UserOutput::printWithPrefix(const std::string& prefix, const std::string& text,
                                 const std::string& color, bool bold) {
    if (level_ == OutputLevel::None) return;
    std::string out;
    if (useColors_) {
        if (bold) out += COLOR_BOLD;
        if (!color.empty()) out += color;
        out += prefix + " " + text;
        if (bold || !color.empty()) out += COLOR_RESET;
    } else {
        out = prefix + " " + text;
    }
    printLine(out);
}

// --- Методы ---

void UserOutput::onMessageReceived(const std::string& text, int64_t chatId) {
    if (level_ < OutputLevel::Main) return;
    std::string prefix = "Now I'm reading this message from chat " + std::to_string(chatId) + ":\n";
    printWithPrefix(prefix, text);
}

void UserOutput::onThinkingStart() {
    if (level_ < OutputLevel::Main) return;
    printWithPrefix(">", "Thinking...", useColors_ ? COLOR_GRAY : "");
}

void UserOutput::onThinkingDone(double seconds) {
    if (level_ < OutputLevel::Tools) return;
    std::string msg = "Thinking done (" + std::to_string(seconds) + "s)";
    printWithPrefix(">", msg, useColors_ ? COLOR_GRAY : "");
}

void UserOutput::onThinkingError(const std::string& errorMessage) {
    if (level_ < OutputLevel::Main) return;
    std::string prefix = ">";
    std::string text = "Thinking error";
    if (useColors_) {
        // оранжевый/жёлтый для слова error
        std::string colored = std::string(COLOR_YELLOW) + "error" + COLOR_RESET;
        text = "Thinking " + colored;
    } else {
        text = "Thinking error";
    }
    printWithPrefix(prefix, text, useColors_ ? COLOR_WHITE : "", false);
    // Дублируем текст ошибки светло-серым
    if (!errorMessage.empty()) {
        printWithPrefix(">", errorMessage, useColors_ ? COLOR_GRAY : "");
    }
}

void UserOutput::onToolCall(const std::string& name, const std::string& args) {
    if (level_ < OutputLevel::Tools) return;
    std::string msg = "Tool: " + name;
    if (!args.empty()) {
        if (useColors_) {
            msg += " " + std::string(COLOR_YELLOW) + "(" + args + ")" + COLOR_RESET;
        } else {
            msg += " (" + args + ")";
        }
    }
    printWithPrefix(">", msg, useColors_ ? COLOR_WHITE : "");
}

void UserOutput::onToolResult(const std::string& result) {
    if (level_ < OutputLevel::Tools) return;
    printWithPrefix("> Tool result:", result, useColors_ ? COLOR_GRAY : "");
}

void UserOutput::onReplySent(const std::string& text) {
    if (level_ < OutputLevel::Main) return;
    printWithPrefix(">", text);
}

void UserOutput::onThought(const std::string& thought) {
    if (level_ < OutputLevel::Deep) return;
    printWithPrefix("> [thought]", thought, useColors_ ? COLOR_GRAY : "");
}

} // namespace common