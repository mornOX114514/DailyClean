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
    int order;                        // 执行顺序（唯一，从小到大依次执行）
    bool enabled;                     // 是否启用该程序
    std::wstring path;                // 程序路径
    std::vector<std::wstring> arguments; // 命令行参数（最多5个）
    ProgramType type;                 // 程序类型
    int delayAfterStart;              // 启动后等待时间（毫秒）
    
    std::wstring processNameToKill;   // 要关闭的进程名
    int killAfterSeconds;             // 启动后多少秒关闭（0表示不关闭）
    
    std::wstring name;                // 配置名称（用于JSON文件名）
    std::wstring description;         // 描述信息

    ProgramConfig(int ord, bool en, const std::wstring& p, const std::vector<std::wstring>& args,
        ProgramType t, int delay, const std::wstring& killProcess = L"",
        int killAfter = 0, const std::wstring& configName = L"",
        const std::wstring& desc = L"")
        : order(ord), enabled(en), path(p), arguments(args), type(t),
        delayAfterStart(delay), processNameToKill(killProcess),
        killAfterSeconds(killAfter), name(configName), description(desc) {}
};

class ProgramLauncher {
private:
    std::vector<ProgramConfig> programs;
    std::wstring configFolderPath;

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

    // 字符串输出工具函数
    void PrintWString(const std::wstring& wstr) {
        std::string str = WStringToUTF8(wstr);
        std::cout << str;
    }

    // 程序类型转换函数
    std::wstring ProgramTypeToString(ProgramType type) {
        switch (type) {
        case ProgramType::Exe: return L"Exe";
        case ProgramType::Bat: return L"Bat";
        case ProgramType::ExeWithArgument: return L"ExeWithArgument";
        default: return L"Unknown";
        }
    }

    ProgramType StringToProgramType(const std::wstring& str) {
        if (str == L"Exe") return ProgramType::Exe;
        if (str == L"Bat") return ProgramType::Bat;
        if (str == L"ExeWithArgument") return ProgramType::ExeWithArgument;
        return ProgramType::Exe;
    }

    // 路径处理函数
    std::wstring GetWorkingDirectory(const std::wstring& path) {
        return path.substr(0, path.find_last_of(L"\\/"));
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
    std::string GetCurrentTime() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char buffer[64];
        sprintf_s(buffer, sizeof(buffer), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
        return std::string(buffer);
    }

    static std::wstring GetCurrentTimeString() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buffer[64];
        swprintf_s(buffer, sizeof(buffer) / sizeof(wchar_t), L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
        return std::wstring(buffer);
    }

    // JSON处理函数
    void SaveConfigToJson(const ProgramConfig& config) {
        std::string jsonPath = WStringToUTF8(configFolderPath + L"\\" + config.name + L".json");
        std::ofstream file(jsonPath);

        if (file.is_open()) {
            file << "{\n";
            file << "  \"order\": " << config.order << ",\n";
            file << "  \"enabled\": " << (config.enabled ? "true" : "false") << ",\n";
            file << "  \"path\": \"" << WStringToUTF8(config.path) << "\",\n";
            file << "  \"type\": \"" << WStringToUTF8(ProgramTypeToString(config.type)) << "\",\n";
            file << "  \"delayAfterStart\": " << config.delayAfterStart << ",\n";
            file << "  \"processNameToKill\": \"" << WStringToUTF8(config.processNameToKill) << "\",\n";
            file << "  \"killAfterSeconds\": " << config.killAfterSeconds << ",\n";
            file << "  \"name\": \"" << WStringToUTF8(config.name) << "\",\n";
            file << "  \"description\": \"" << WStringToUTF8(config.description) << "\",\n";

            file << "  \"arguments\": [\n";
            for (size_t i = 0; i < config.arguments.size(); i++) {
                file << "    \"" << WStringToUTF8(config.arguments[i]) << "\"";
                if (i < config.arguments.size() - 1) file << ",";
                file << "\n";
            }
            file << "  ]\n";
            file << "}\n";

            file.close();
            std::wcout << L"✓ 已保存配置: " << config.name << L".json" << std::endl;
        }
    }

    ProgramConfig LoadConfigFromJson(const std::wstring& jsonPath) {
        std::string utf8Path = WStringToUTF8(jsonPath);
        std::ifstream file(utf8Path);
        ProgramConfig config(0, true, L"", {}, ProgramType::Exe, 2000);

        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                ParseJsonLine(line, config);
            }
            file.close();
        }

        return config;
    }

    void ParseJsonLine(const std::string& line, ProgramConfig& config) {
        auto extractStringValue = [&](const std::string& key) -> std::wstring {
            size_t start = line.find("\"", line.find(key) + key.length() + 1) + 1;
            size_t end = line.find("\"", start);
            return (start != std::string::npos && end != std::string::npos) ?
                UTF8ToWString(line.substr(start, end - start)) : L"";
            };

        auto extractIntValue = [&](const std::string& key) -> int {
            size_t pos = line.find(":");
            if (pos != std::string::npos) {
                std::string value = line.substr(pos + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t,") + 1);
                return std::stoi(value);
            }
            return 0;
            };

        if (line.find("\"order\"") != std::string::npos) {
            config.order = extractIntValue("\"order\"");
        }
        else if (line.find("\"enabled\"") != std::string::npos) {
            config.enabled = (line.find("true") != std::string::npos);
        }
        else if (line.find("\"path\"") != std::string::npos) {
            config.path = extractStringValue("\"path\"");
        }
        else if (line.find("\"type\"") != std::string::npos) {
            config.type = StringToProgramType(extractStringValue("\"type\""));
        }
        else if (line.find("\"name\"") != std::string::npos) {
            config.name = extractStringValue("\"name\"");
        }
        else if (line.find("\"description\"") != std::string::npos) {
            config.description = extractStringValue("\"description\"");
        }
        else if (line.find("\"processNameToKill\"") != std::string::npos) {
            config.processNameToKill = extractStringValue("\"processNameToKill\"");
        }
        else if (line.find("\"killAfterSeconds\"") != std::string::npos) {
            config.killAfterSeconds = extractIntValue("\"killAfterSeconds\"");
        }
        else if (line.find("\"delayAfterStart\"") != std::string::npos) {
            config.delayAfterStart = extractIntValue("\"delayAfterStart\"");
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
                            std::wcout << L"✓ 已关闭进程: " << processName << std::endl;
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

    static void KillProcessAfterDelay(ProgramLauncher* launcher, const ProgramConfig& config, int delayMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        std::wcout << L"[" << GetCurrentTimeString() << L"] 正在关闭进程: " << config.processNameToKill << std::endl;
        if (!launcher->KillProcessByName(config.processNameToKill)) {
            std::wcout << L"✗ 未找到进程: " << config.processNameToKill << std::endl;
        }
    }

    // 配置管理函数
    void InitializeConfigFolder() {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring exeDir = exePath;
        exeDir = exeDir.substr(0, exeDir.find_last_of(L"\\/"));

        configFolderPath = exeDir + L"\\ProgramConfigs";
        CreateDirectoryW(configFolderPath.c_str(), NULL);
    }

    void LoadConfigsFromFolder() {
        WIN32_FIND_DATAW findFileData;
        HANDLE hFind = FindFirstFileW((configFolderPath + L"\\*.json").c_str(), &findFileData);

        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::wstring fileName = findFileData.cFileName;
                    std::wstring configName = fileName.substr(0, fileName.find_last_of(L'.'));

                    bool exists = std::any_of(programs.begin(), programs.end(),
                        [&](const ProgramConfig& c) { return c.name == configName; });

                    if (!exists) {
                        std::wstring jsonPath = configFolderPath + L"\\" + fileName;
                        programs.push_back(LoadConfigFromJson(jsonPath));
                        std::wcout << L"✓ 从文件加载配置: " << configName << std::endl;
                    }
                }
            } while (FindNextFileW(hFind, &findFileData));
            FindClose(hFind);
        }
    }

    // 程序启动函数
    bool StartProgram(const ProgramConfig& config) {
        try {
            STARTUPINFOW si = { sizeof(si) };
            PROCESS_INFORMATION pi;
            std::wstring commandLine = BuildCommandLine(config);
            std::wstring workingDirectory = GetWorkingDirectory(config.path);

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
                std::cerr << "CreateProcess失败，错误代码: " << GetLastError() << std::endl;
                return false;
            }
        }
        catch (...) {
            return false;
        }
    }

    // 显示函数
    void DisplayProgramInfo(const ProgramConfig& config, size_t index, size_t total) {
        std::cout << index + 1 << ". [Order:" << config.order << "] ";
        PrintWString(config.name);
        std::cout << std::endl;

        std::cout << "   描述: ";
        PrintWString(config.description);
        std::cout << std::endl;

        std::cout << "   路径: ";
        PrintWString(config.path);
        std::cout << std::endl;

        if (config.type == ProgramType::ExeWithArgument && !config.arguments.empty()) {
            std::cout << "   参数 (" << config.arguments.size() << "个): ";
            for (size_t j = 0; j < config.arguments.size() && j < 5; j++) {
                if (j > 0) std::cout << ", ";
                PrintWString(config.arguments[j]);
            }
            std::cout << std::endl;
        }

        if (config.killAfterSeconds > 0 && !config.processNameToKill.empty()) {
            std::cout << "   自动关闭: " << config.killAfterSeconds << "秒后关闭 ";
            PrintWString(config.processNameToKill);
            std::cout << std::endl;
        }

        std::cout << "   启动后等待: " << config.delayAfterStart << " 毫秒" << std::endl;
        std::cout << std::endl;
    }

    void DisplayStartupInfo(const ProgramConfig& config, size_t index, size_t total) {
        std::cout << "[" << GetCurrentTime() << "] 准备启动 (" << index + 1 << "/" << total
            << "): [Order:" << config.order << "] ";
        PrintWString(config.name);
        std::cout << std::endl;

        std::cout << "描述: ";
        PrintWString(config.description);
        std::cout << std::endl;

        std::cout << "命令: ";
        PrintWString(BuildCommandLine(config));
        std::cout << std::endl;

        if (config.killAfterSeconds > 0 && !config.processNameToKill.empty()) {
            std::cout << "自动关闭: " << config.killAfterSeconds << "秒后关闭 ";
            PrintWString(config.processNameToKill);
            std::cout << std::endl;
        }
    }

public:
    ProgramLauncher() {
        InitializeConfigFolder();
    }

    void SetConsoleUTF8() {
        SetConsoleOutputCP(65001);
        SetConsoleCP(65001);
    }

    void InitializePrograms() {
        std::vector<ProgramConfig> defaultPrograms = {

        };

        programs.insert(programs.end(), defaultPrograms.begin(), defaultPrograms.end());

        for (const auto& config : defaultPrograms) {
            SaveConfigToJson(config);
        }

        LoadConfigsFromFolder();
    }

    void Run() {
        SetConsoleUTF8();
        SetConsoleTitleW(L"游戏助手启动器 - 优化版");

        // 显示标题和特性
        std::cout << "游戏助手启动器 v3.0 - 优化版" << std::endl;
        std::cout << "配置文件夹: ";
        PrintWString(configFolderPath);
        std::cout << std::endl << std::endl;

        // 过滤和排序程序
        auto enabledPrograms = programs;
        enabledPrograms.erase(std::remove_if(enabledPrograms.begin(), enabledPrograms.end(),
            [](const ProgramConfig& c) { return !c.enabled; }), enabledPrograms.end());

        std::sort(enabledPrograms.begin(), enabledPrograms.end(),
            [](const ProgramConfig& a, const ProgramConfig& b) { return a.order < b.order; });

        // 显示配置信息
        std::cout << "当前启用的程序配置 (" << enabledPrograms.size() << "个):" << std::endl;
        std::cout << "=====================================" << std::endl;

        for (size_t i = 0; i < enabledPrograms.size(); i++) {
            DisplayProgramInfo(enabledPrograms[i], i, enabledPrograms.size());
        }

        // 启动程序
        std::cout << "=====================================" << std::endl;
        std::cout << "开始启动程序..." << std::endl << std::endl;

        for (size_t i = 0; i < enabledPrograms.size(); i++) {
            const auto& config = enabledPrograms[i];
            DisplayStartupInfo(config, i, enabledPrograms.size());

            if (config.type != ProgramType::Bat && !FileExists(config.path)) {
                std::cout << "✗ 文件不存在: ";
                PrintWString(config.path);
                std::cout << std::endl;
                continue;
            }

            if (StartProgram(config)) {
                std::cout << "✓ 成功启动: ";
                PrintWString(config.name);
                std::cout << std::endl;
            }
            else {
                std::cout << "✗ 启动失败: ";
                PrintWString(config.name);
                std::cout << std::endl;
            }

            if (i < enabledPrograms.size() - 1) {
                int delaySeconds = config.delayAfterStart / 1000;
                std::cout << "等待 " << delaySeconds << " 秒后启动下一个程序..." << std::endl << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(config.delayAfterStart));
            }
        }

        std::cout << "=====================================" << std::endl;
        std::cout << "所有程序启动完成！" << std::endl;
        std::cout << "按任意键退出..." << std::endl;
        std::cin.get();
    }
};

int main() {
    ProgramLauncher launcher;
    launcher.InitializePrograms();
    launcher.Run();
    return 0;
}