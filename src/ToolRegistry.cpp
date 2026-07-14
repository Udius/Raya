#include "core/LLMMessageHandler.h"
#include <stdexcept>

namespace core {

void ToolRegistry::registerTool(const std::string& name, ToolHandler handler) {
    handlers_[name] = std::move(handler);
}

std::string ToolRegistry::execute(const std::string& name, const nlohmann::json& args) const {
    auto it = handlers_.find(name);
    if (it == handlers_.end()) {
        throw std::runtime_error("Tool not found: " + name);
    }
    return it->second(args);
}

bool ToolRegistry::hasTool(const std::string& name) const {
    return handlers_.find(name) != handlers_.end();
}

} // namespace core