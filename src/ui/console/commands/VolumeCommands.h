#pragma once
#include <map>
#include <string>
#include <memory>

class IConsoleCommand;

void RegisterVolumeCommands(std::map<std::string, std::unique_ptr<IConsoleCommand>>& commands);
