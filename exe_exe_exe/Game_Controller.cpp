#include <iostream>
#include <vector>
#include <string>
#include <windows.h>
#include <algorithm>
#include <thread>
#include <chrono>
#include <locale>
#include <codecvt>
#include <tlhelp32.h>
#include <fstream>

// 程序类型枚举
enum class ProgramType {
    Exe,              // 普通EXE程序
    Bat,              // BAT脚本文件
    ExeWithArgument   // 需要参数的EXE程序
};

// 程序配置类
struct ProgramConfig {
    int order;                        // 执行顺序
    bool enabled;                     // 是否启用该程序

    std::wstring path;                // 程序路径
    std::vector<std::wstring> arguments; // 命令行参数
    ProgramType type;                 // 程序类型

    int delayAfterStart;              // 启动后等待时间（毫秒）

    std::wstring processNameToKill;   // 要关闭的进程名
    int killAfterSeconds;             // 启动后多少秒关闭（0表示不关闭）

    std::wstring name;                // 配置名称
    std::wstring description;         // 描述信息

    ProgramConfig() : order(0), enabled(true), type(ProgramType::Exe),
        delayAfterStart(2000), killAfterSeconds(0) {}
};

class GameController {
private:
    ProgramConfig config;

    // 编码转换工具函数
    std::string WStringToUTF8(const std::wstring& wstr) {
        if (wstr.empty()) return "";
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string str(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], size_needed, NULL, NULL);
        return str;
    }

    std::wstring UTF8ToWString(const std::string& str) {
        if (str.empty()) return L"";
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size_needed);
        return wstr;
    }

    // 程序类型转换函数
    ProgramType StringToProgramType(const std::wstring& str) {
        if (str == L"Exe") return ProgramType::Exe;
        if (str == L"Bat") return ProgramType::Bat;
        if (str == L"ExeWithArgument") return ProgramType::ExeWithArgument;
        return ProgramType::Exe;
    }

    // 路径处理函数
    std::wstring GetWorkingDirectory(const std::wstring& path) {
        size_t lastSlash = path.find_last_of(L"\\/");
        return (lastSlash != std::wstring::npos) ? path.substr(0, lastSlash) : L"";
    }

    // 参数处理函数
    std::wstring BuildArgumentsString(const std::vector<std::wstring>& arguments) {
        if (arguments.empty()) return L"";

        std::wstring result;
        for (size_t i = 0; i < arguments.size() && i < 5; i++) {
            if (!result.empty()) result += L" ";
            if (arguments[i].find(L' ') != std::wstring::npos) {
                result += L"\"" + arguments[i] + L"\"";
            }
            else {
                result += arguments[i];
            }
        }
        return result;
    }

    // 命令构建函数
    std::wstring BuildCommandLine(const ProgramConfig& config) {
        switch (config.type) {
        case ProgramType::Bat:
            return L"cmd.exe /c \"" + config.path + L"\"";
        case ProgramType::Exe:
            return config.path;
        case ProgramType::ExeWithArgument:
            std::wstring argsString = BuildArgumentsString(config.arguments);
            return argsString.empty() ? config.path : config.path + L" " + argsString;
        }
        return config.path;
    }

    // 文件操作函数
    bool FileExists(const std::wstring& path) {
        DWORD attrib = GetFileAttributesW(path.c_str());
        return (attrib != INVALID_FILE_ATTRIBUTES && !(attrib & FILE_ATTRIBUTE_DIRECTORY));
    }

    // 时间函数
    static std::wstring GetCurrentTimeString() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buffer[64];
        swprintf_s(buffer, sizeof(buffer) / sizeof(wchar_t), L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
        return std::wstring(buffer);
    }

    // 新的JSON处理函数
    void LoadConfigFromJson(const std::wstring& jsonPath) {
        std::string utf8Path = WStringToUTF8(jsonPath);
        std::ifstream file(utf8Path);

        if (!file.is_open()) {
            std::wcerr << L"Cannot open config file: " << jsonPath << std::endl;
            return;
        }

        // 读取整个文件内容
        std::string content((std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        file.close();

        // 解析JSON内容
        ParseJsonContent(content, config);
    }

    void ParseJsonContent(const std::string& content, ProgramConfig& config) {
        // 简化的JSON解析 - 处理关键字段
        auto getStringValue = [&](const std::string& key) -> std::wstring {
            std::string searchStr = "\"" + key + "\": \"";
            size_t start = content.find(searchStr);
            if (start == std::string::npos) {
                // 尝试不带引号的值
                searchStr = "\"" + key + "\": ";
                start = content.find(searchStr);
                if (start == std::string::npos) return L"";
                start += searchStr.length();
                size_t end = content.find_first_of(",}\n\r", start);
                if (end == std::string::npos) return L"";
                std::string value = content.substr(start, end - start);
                // 去除可能的引号和空格
                value.erase(0, value.find_first_not_of(" \t\""));
                value.erase(value.find_last_not_of(" \t\"") + 1);
                return UTF8ToWString(value);
            }
            start += searchStr.length();
            size_t end = content.find("\"", start);
            if (end == std::string::npos) return L"";
            return UTF8ToWString(content.substr(start, end - start));
            };

        auto getIntValue = [&](const std::string& key) -> int {
            std::string searchStr = "\"" + key + "\": ";
            size_t start = content.find(searchStr);
            if (start == std::string::npos) return 0;
            start += searchStr.length();
            size_t end = content.find_first_of(",}\n\r", start);
            if (end == std::string::npos) return 0;
            std::string value = content.substr(start, end - start);
            try {
                // 去除空格
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);
                return std::stoi(value);
            }
            catch (...) {
                return 0;
            }
            };

        auto getBoolValue = [&](const std::string& key) -> bool {
            std::string searchStr = "\"" + key + "\": ";
            size_t start = content.find(searchStr);
            if (start == std::string::npos) return false;
            start += searchStr.length();
            size_t end = content.find_first_of(",}\n\r", start);
            if (end == std::string::npos) return false;
            std::string value = content.substr(start, end - start);
            // 去除空格
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            return value.find("true") != std::string::npos;
            };

        // 解析arguments数组
        auto getArrayValues = [&](const std::string& key) -> std::vector<std::wstring> {
            std::vector<std::wstring> result;
            std::string searchStr = "\"" + key + "\": [";
            size_t arrayStart = content.find(searchStr);
            if (arrayStart == std::string::npos) return result;

            arrayStart += searchStr.length();
            size_t arrayEnd = content.find("]", arrayStart);
            if (arrayEnd == std::string::npos) return result;

            std::string arrayContent = content.substr(arrayStart, arrayEnd - arrayStart);

            size_t pos = 0;
            while ((pos = arrayContent.find("\"", pos)) != std::string::npos) {
                size_t valueStart = pos + 1;
                size_t valueEnd = arrayContent.find("\"", valueStart);
                if (valueEnd == std::string::npos) break;

                std::string item = arrayContent.substr(valueStart, valueEnd - valueStart);
                result.push_back(UTF8ToWString(item));
                pos = valueEnd + 1;
            }

            return result;
            };

        // 解析各个字段
        config.order = getIntValue("order");
        config.enabled = getBoolValue("enabled");
        config.path = getStringValue("path");

        std::wstring typeStr = getStringValue("type");
        config.type = StringToProgramType(typeStr);

        config.name = getStringValue("name");
        config.description = getStringValue("description");
        config.processNameToKill = getStringValue("processNameToKill");
        config.killAfterSeconds = getIntValue("killAfterSeconds");
        config.delayAfterStart = getIntValue("delayAfterStart");

        // 解析arguments
        config.arguments = getArrayValues("arguments");

        // 调试输出解析结果
        std::wcout << L"[DEBUG] JSON Parsing Results:" << std::endl;
        std::wcout << L"  Path: " << config.path << std::endl;
        std::wcout << L"  Type: " << (int)config.type << std::endl;
        std::wcout << L"  Arguments count: " << config.arguments.size() << std::endl;
        for (size_t i = 0; i < config.arguments.size(); i++) {
            std::wcout << L"  Argument " << i << L": '" << config.arguments[i] << L"'" << std::endl;
        }
    }

    // 进程管理函数
    bool KillProcessByName(const std::wstring& processName) {
        bool found = false;
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) return false;

        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(PROCESSENTRY32W);

        if (Process32FirstW(hSnapshot, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, processName.c_str()) == 0) {
                    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProcess) {
                        if (TerminateProcess(hProcess, 0)) {
                            std::wcout << L"[" << GetCurrentTimeString() << L"] Process closed: " << processName << std::endl;
                            found = true;
                        }
                        CloseHandle(hProcess);
                    }
                }
            } while (Process32NextW(hSnapshot, &pe));
        }

        CloseHandle(hSnapshot);
        return found;
    }

    static void KillProcessAfterDelay(GameController* controller, const ProgramConfig& config, int delayMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        std::wcout << L"[" << GetCurrentTimeString() << L"] Closing process: " << config.processNameToKill << std::endl;
        if (!controller->KillProcessByName(config.processNameToKill)) {
            std::wcout << L"[" << GetCurrentTimeString() << L"] Process not found: " << config.processNameToKill << std::endl;
        }
    }

    // 程序启动函数
    bool StartProgram(const ProgramConfig& config) {
        try {
            STARTUPINFOW si = { sizeof(si) };
            PROCESS_INFORMATION pi;
            std::wstring commandLine = BuildCommandLine(config);
            std::wstring workingDirectory = GetWorkingDirectory(config.path);

            std::wcout << L"[" << GetCurrentTimeString() << L"] Start command: " << commandLine << std::endl;

            BOOL success = CreateProcessW(
                NULL,
                const_cast<LPWSTR>(commandLine.c_str()),
                NULL,
                NULL,
                FALSE,
                0,
                NULL,
                workingDirectory.empty() ? NULL : workingDirectory.c_str(),
                &si,
                &pi
            );

            if (success) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);

                if (config.killAfterSeconds > 0 && !config.processNameToKill.empty()) {
                    std::thread(KillProcessAfterDelay, this, config, config.killAfterSeconds * 1000).detach();
                }
                return true;
            }
            else {
                std::wcerr << L"[" << GetCurrentTimeString() << L"] CreateProcess failed, error code: " << GetLastError() << std::endl;
                return false;
            }
        }
        catch (...) {
            return false;
        }
    }

    // 显示函数
    void DisplayProgramInfo() {
        std::wcout << L"=====================================" << std::endl;
        std::wcout << L"Game Controller - Independent Control Window" << std::endl;
        std::wcout << L"Config Name: " << config.name << std::endl;
        std::wcout << L"Description: " << config.description << std::endl;
        std::wcout << L"Program Path: " << config.path << std::endl;

        if (config.type == ProgramType::ExeWithArgument && !config.arguments.empty()) {
            std::wcout << L"Arguments: ";
            for (size_t j = 0; j < config.arguments.size() && j < 5; j++) {
                if (j > 0) std::wcout << L", ";
                std::wcout << config.arguments[j];
            }
            std::wcout << std::endl;
        }

        if (config.killAfterSeconds > 0 && !config.processNameToKill.empty()) {
            std::wcout << L"Auto Close: " << config.killAfterSeconds << L" seconds later close " << config.processNameToKill << std::endl;
        }
        std::wcout << L"=====================================" << std::endl;
    }

public:
    void SetConsoleUTF8() {
        SetConsoleOutputCP(65001);
        SetConsoleCP(65001);
    }

    bool Initialize(const std::wstring& configPath) {
        SetConsoleUTF8();

        // 设置窗口标题
        std::wstring title = L"Game Controller - ";
        title += configPath.substr(configPath.find_last_of(L"\\/") + 1);
        SetConsoleTitleW(title.c_str());

        // 加载配置
        LoadConfigFromJson(configPath);

        if (config.path.empty()) {
            std::wcerr << L"Error: Invalid configuration file" << std::endl;
            return false;
        }

        return true;
    }

    void Run() {
        DisplayProgramInfo();

        std::wcout << L"[" << GetCurrentTimeString() << L"] Starting program..." << std::endl;

        if (config.type != ProgramType::Bat && !FileExists(config.path)) {
            std::wcout << L"[" << GetCurrentTimeString() << L"] File does not exist: " << config.path << std::endl;
            std::wcout << L"Press any key to exit..." << std::endl;
            std::cin.get();
            return;
        }

        if (StartProgram(config)) {
            std::wcout << L"[" << GetCurrentTimeString() << L"] Program started successfully" << std::endl;
        }
        else {
            std::wcout << L"[" << GetCurrentTimeString() << L"] Failed to start program" << std::endl;
        }

        if (config.killAfterSeconds > 0) {
            std::wcout << L"[" << GetCurrentTimeString() << L"] Controller will run in background, waiting to auto close process..." << std::endl;
            std::wcout << L"Press any key to exit controller immediately..." << std::endl;
        }
        else {
            std::wcout << L"Press any key to exit..." << std::endl;
        }

        std::cin.get();
    }
};

int main(int argc, char* argv[]) {
    // 设置控制台编码
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);

    if (argc < 2) {
        std::cout << "Usage: GameController.exe <config file path>" << std::endl;
        std::cout << "Press any key to exit..." << std::endl;
        std::cin.get();
        return 1;
    }

    // UTF-8 到 wstring 的转换函数
    auto UTF8ToWString = [](const std::string& str) -> std::wstring {
        if (str.empty()) return L"";
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size_needed);
        return wstr;
        };

    // 将 char* 参数转换为 wstring
    std::string configPathUtf8 = argv[1];
    std::wstring configPath = UTF8ToWString(configPathUtf8);

    GameController controller;
    if (controller.Initialize(configPath)) {
        controller.Run();
    }
    else {
        std::cout << "Initialization failed, press any key to exit..." << std::endl;
        std::cin.get();
        return 1;
    }

    return 0;
}