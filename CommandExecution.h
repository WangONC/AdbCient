#pragma once
#include <atlbase.h>
#include <atlstr.h>
#include <Windows.h>
#include <vector>
#include <functional>
#include <future>
using namespace ATL;


class CommandExecution
{
public:
    struct CommandResult {
        DWORD result = -1;
        std::string errorMsg;
        std::string successMsg;
    };

    // 同步方法
    static CommandResult ExecCommand(const std::string& command, bool asAdmin = false);
    static CommandResult ExecCommand(const std::vector<std::string>& commands, bool asAdmin = false);

    // 异步方法
    static std::future<CommandResult> ExecCommandAsync(const std::string& command, bool asAdmin = false);
    static std::future<CommandResult> ExecCommandAsync(const std::vector<std::string>& commands, bool asAdmin = false);

private:
    static CommandResult ExecuteCommands(const std::string& commandLine, bool asAdmin);
    static void AsyncExecutor(const std::string& commandLine, bool asAdmin, std::promise<CommandResult>&& resultPromise);
    static std::string EscapePowerShellCommand(const std::string& command);
};

