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
    std::wstring gameControllerPath;
    
    // 可自定义的字符串变量
    std::wstring gameControllerName = L"GameController.exe";  // 游戏控制器可执行文件名
    std::wstring configFolderName = L"ProgramConfigs";        // 配置文件夹名称

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

    // JSON处理函数
    void SaveConfigToJson(const ProgramConfig& config, const std::wstring& filePath = L"") {
        std::wstring actualPath = filePath.empty() ? 
            configFolderPath + L"\\" + config.name + L".json" : filePath;
            
        std::string jsonPath = WStringToUTF8(actualPath);
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
            std::wcout << L"✓ 已保存配置: " << actualPath << std::endl;
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

    // 配置管理函数
    void InitializeConfigFolder() {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring exeDir = exePath;
        exeDir = exeDir.substr(0, exeDir.find_last_of(L"\\/"));

        // 使用可自定义的文件夹名称
        configFolderPath = exeDir + L"\\" + configFolderName;
        CreateDirectoryW(configFolderPath.c_str(), NULL);
        
        // 使用可自定义的游戏控制器名称
        gameControllerPath = exeDir + L"\\" + gameControllerName;
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

    // 调用游戏控制器
    bool CallGameController(const ProgramConfig& config) {
        // 直接使用现有的JSON配置文件，不再创建临时文件
        std::wstring configFilePath = configFolderPath + L"\\" + config.name + L".json";
        
        // 构建命令行
        std::wstring commandLine = L"\"" + gameControllerPath + L"\" \"" + configFilePath + L"\"";
        
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        
        BOOL success = CreateProcessW(
            NULL,
            const_cast<LPWSTR>(commandLine.c_str()),
            NULL,
            NULL,
            FALSE,
            CREATE_NEW_CONSOLE,  // 在新控制台窗口中启动
            NULL,
            NULL,
            &si,
            &pi
        );

        if (success) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return true;
        } else {
            std::cerr << "调用游戏控制器失败，错误代码: " << GetLastError() << std::endl;
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

        if (config.killAfterSeconds > 0 && !config.processNameToKill.empty()) {
            std::cout << "自动关闭: " << config.killAfterSeconds << "秒后关闭 ";
            PrintWString(config.processNameToKill);
            std::cout << std::endl;
        }
    }

    std::string GetCurrentTime() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char buffer[64];
        sprintf_s(buffer, sizeof(buffer), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
        return std::string(buffer);
    }

public:
    ProgramLauncher() {
        InitializeConfigFolder();
    }

    // 设置自定义名称的方法
    void SetGameControllerName(const std::wstring& name) {
        gameControllerName = name;
        // 重新初始化路径
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring exeDir = exePath;
        exeDir = exeDir.substr(0, exeDir.find_last_of(L"\\/"));
        gameControllerPath = exeDir + L"\\" + gameControllerName;
    }

    void SetConfigFolderName(const std::wstring& name) {
        configFolderName = name;
        // 重新初始化配置文件夹路径
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring exeDir = exePath;
        exeDir = exeDir.substr(0, exeDir.find_last_of(L"\\/"));
        configFolderPath = exeDir + L"\\" + configFolderName;
        CreateDirectoryW(configFolderPath.c_str(), NULL);
    }

    void SetConsoleUTF8() {
        SetConsoleOutputCP(65001);
        SetConsoleCP(65001);
    }

    void InitializePrograms() {
        std::vector<ProgramConfig> defaultPrograms = {
            // 这里可以保留一些默认配置
        };

        programs.insert(programs.end(), defaultPrograms.begin(), defaultPrograms.end());

        for (const auto& config : defaultPrograms) {
            SaveConfigToJson(config);
        }

        LoadConfigsFromFolder();
    }

    void Run() {
        SetConsoleUTF8();
        SetConsoleTitleW(L"游戏助手启动器 - 主控制器");

        // 显示自定义设置信息
        std::cout << "游戏控制器: ";
        PrintWString(gameControllerName);
        std::cout << std::endl;
        std::cout << "配置文件夹: ";
        PrintWString(configFolderName);
        std::cout << std::endl;

        // 检查游戏控制器是否存在
        if (GetFileAttributesW(gameControllerPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::wcout << L"错误: 未找到 " << gameControllerName << L"，请确保它与主程序在同一目录下。" << std::endl;
            std::cout << "按任意键退出..." << std::endl;
            std::cin.get();
            return;
        }

        // 显示标题和特性
        std::cout << "游戏助手启动器 v4.0 - 分布式控制版" << std::endl;
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

            if (CallGameController(config)) {
                std::cout << "✓ 已启动游戏控制器: ";
                PrintWString(config.name);
                std::cout << std::endl;
            } else {
                std::cout << "✗ 启动游戏控制器失败: ";
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
    
    // 在这里可以自定义名称（可选）
    //launcher.SetGameControllerName(L"GameMJ_Controller.exe");
    // launcher.SetConfigFolderName(L"MyConfigs");
    
    launcher.InitializePrograms();
    launcher.Run();
    return 0;
}