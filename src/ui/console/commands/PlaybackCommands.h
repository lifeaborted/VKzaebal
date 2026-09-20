#pragma once
#include <map>
#include <string>
#include <memory>

class IConsoleCommand;

void RegisterPlaybackCommands(std::map<std::string, std::unique_ptr<IConsoleCommand>>& commands);
