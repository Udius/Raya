#pragma once

#include <string>
#include <chrono>

namespace common {

enum class OutputLevel {
    None,   // совсем ничего
    Tools,  // только вызовы инструментов и время мышления
    Main,   // основные события
    Deep    // все, включая внутренние мысли (заглушка)
};

class UserOutput {
public:
    explicit UserOutput(OutputLevel level);

    // Основные события
    void onMessageReceived(const std::string& text, int64_t chatId);
    void onThinkingStart();
    void onThinkingDone(double seconds);
    void onThinkingError(const std::string& errorMessage);
    void onToolCall(const std::string& name, const std::string& args);
    void onToolResult(const std::string& result);
    void onReplySent(const std::string& text);

    // Для deep режима (заглушка)
    void onThought(const std::string& thought);

    // Статический метод для парсинга строки уровня
    static OutputLevel fromString(const std::string& str);

private:
    OutputLevel level_;
    bool useColors_;

    void printLine(const std::string& line);
    void printWithPrefix(const std::string& prefix, const std::string& text,
                         const std::string& color = "", bool bold = false);
};

} // namespace common