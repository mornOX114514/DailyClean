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

    ProgramConfig(int ord = 0, bool en = true, const std::wstring& p = L"",
        const std::vector<std::wstring>& args = {}, ProgramType t = ProgramType::Exe,
        int delay = 2000, const std::wstring& killProcess = L"", int killAfter = 0,
        const std::wstring& configName = L"", const std::wstring& desc = L"")
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

    // JSON处理函数 - 使用标准ofstream，避免宽字符问题
    bool SaveConfigToJson(const ProgramConfig& config) {
        std::wstring filePath = configFolderPath + L"\\" + config.name + L".json";
        std::string utf8FilePath = WStringToUTF8(filePath);
        std::ofstream file(utf8FilePath);

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
            std::cout << "Saved config: " << utf8FilePath << std::endl;
            return true;
        }
        else {
            std::cout << "Failed to save config: " << utf8FilePath << std::endl;
            return false;
        }
    }

    ProgramConfig LoadConfigFromJson(const std::wstring& jsonPath) {
        std::string utf8Path = WStringToUTF8(jsonPath);
        std::ifstream file(utf8Path);
        ProgramConfig config;

        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                ParseJsonLine(line, config);
            }
            file.close();
        }
        else {
            std::cout << "Cannot open config file: " << utf8Path << std::endl;
        }

        return config;
    }

    void ParseJsonLine(const std::string& line, ProgramConfig& config) {
        auto extractStringValue = [&](const std::string& key) -> std::wstring {
            size_t keyPos = line.find(key);
            if (keyPos == std::string::npos) return L"";

            size_t start = line.find("\"", keyPos + key.length() + 1) + 1;
            size_t end = line.find("\"", start);
            return (start != std::string::npos && end != std::string::npos) ?
                UTF8ToWString(line.substr(start, end - start)) : L"";
            };

        auto extractIntValue = [&](const std::string& key) -> int {
            size_t pos = line.find(key);
            if (pos == std::string::npos) return 0;

            pos = line.find(":", pos);
            if (pos == std::string::npos) return 0;

            std::string value = line.substr(pos + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t,") + 1);
            try {
                return std::stoi(value);
            }
            catch (...) {
                return 0;
            }
            };

        auto extractBoolValue = [&](const std::string& key) -> bool {
            size_t pos = line.find(key);
            if (pos == std::string::npos) return false;
            return line.find("true", pos) != std::string::npos;
            };

        if (line.find("\"order\"") != std::string::npos) {
            config.order = extractIntValue("\"order\"");
        }
        else if (line.find("\"enabled\"") != std::string::npos) {
            config.enabled = extractBoolValue("\"enabled\"");
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
    bool InitializeConfigFolder() {
        wchar_t exePath[MAX_PATH];
        if (GetModuleFileNameW(NULL, exePath, MAX_PATH) == 0) {
            std::cout << "Error: Cannot get executable path" << std::endl;
            return false;
        }

        std::wstring exeDir = exePath;
        size_t lastSlash = exeDir.find_last_of(L"\\/");
        if (lastSlash == std::wstring::npos) {
            std::cout << "Error: Invalid executable path" << std::endl;
            return false;
        }

        exeDir = exeDir.substr(0, lastSlash);

        // 使用可自定义的文件夹名称
        configFolderPath = exeDir + L"\\" + configFolderName;

        // 创建文件夹（如果不存在）
        DWORD attr = GetFileAttributesW(configFolderPath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            if (CreateDirectoryW(configFolderPath.c_str(), NULL)) {
                std::wcout << L"Created config folder: " << configFolderPath << std::endl;
            }
            else {
                std::wcout << L"Failed to create config folder: " << configFolderPath << std::endl;
                return false;
            }
        }

        // 使用可自定义的游戏控制器名称
        gameControllerPath = exeDir + L"\\" + gameControllerName;
        return true;
    }

    // 检查文件夹是否为空（只检查JSON文件）
    bool IsConfigFolderEmpty() {
        WIN32_FIND_DATAW findFileData;
        HANDLE hFind = FindFirstFileW((configFolderPath + L"\\*.json").c_str(), &findFileData);

        if (hFind == INVALID_HANDLE_VALUE) {
            return true; // 没有找到JSON文件，视为空
        }

        bool foundJson = false;
        do {
            if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::wstring fileName = findFileData.cFileName;
                if (fileName.length() > 5 && fileName.substr(fileName.length() - 5) == L".json") {
                    foundJson = true;
                    break;
                }
            }
        } while (FindNextFileW(hFind, &findFileData));

        FindClose(hFind);
        return !foundJson;
    }

    // 创建默认配置文件 - 完全使用英文
    void CreateDefaultConfigFile() {
        std::cout << "Config folder is empty, creating default config files..." << std::endl;

        // 创建几个示例配置文件 - 完全使用英文
        std::vector<ProgramConfig> defaultConfigs = {
            ProgramConfig(
                1,
                true,
                L"C:\\Program Files\\MyGame\\game.exe",
                {L"-windowed", L"-fullscreen"},
                ProgramType::ExeWithArgument,
                3000,
                L"game.exe",
                0,
                L"ExampleGame",
                L"Example game configuration with command line arguments"
            ),
            ProgramConfig(
                2,
                true,
                L"C:\\Program Files\\MyGame\\launcher.bat",
                {},
                ProgramType::Bat,
                2000,
                L"",
                0,
                L"BatchLauncher",
                L"Batch file launcher example"
            ),
            ProgramConfig(
                3,
                false,
                L"C:\\Program Files\\MyGame\\tool.exe",
                {L"-mode", L"background", L"-port", L"8080"},
                ProgramType::ExeWithArgument,
                1000,
                L"tool.exe",
                60,
                L"BackgroundTool",
                L"Background tool with auto-close after 60 seconds"
            )
        };

        int successCount = 0;
        for (const auto& config : defaultConfigs) {
            if (SaveConfigToJson(config)) {
                successCount++;
            }
        }

        std::cout << "Created " << successCount << " default config files." << std::endl;
        std::cout << "Please edit JSON files in config folder to configure your programs." << std::endl;
        std::cout << std::endl;
    }

    void LoadConfigsFromFolder() {
        std::cout << "Checking config folder..." << std::endl;

        // 首先检查文件夹是否为空（只检查JSON文件）
        if (IsConfigFolderEmpty()) {
            std::cout << "No JSON files found in config folder" << std::endl;
            CreateDefaultConfigFile();
        }
        else {
            std::cout << "JSON files found, skipping default config creation" << std::endl;
        }

        // 加载所有JSON配置文件
        WIN32_FIND_DATAW findFileData;
        HANDLE hFind = FindFirstFileW((configFolderPath + L"\\*.json").c_str(), &findFileData);

        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::wstring fileName = findFileData.cFileName;
                    if (fileName.length() > 5 && fileName.substr(fileName.length() - 5) == L".json") {
                        std::wstring configName = fileName.substr(0, fileName.length() - 5);

                        bool exists = std::any_of(programs.begin(), programs.end(),
                            [&](const ProgramConfig& c) { return c.name == configName; });

                        if (!exists) {
                            std::wstring jsonPath = configFolderPath + L"\\" + fileName;
                            ProgramConfig loadedConfig = LoadConfigFromJson(jsonPath);
                            programs.push_back(loadedConfig);
                            std::cout << "Loaded config: " << WStringToUTF8(configName) << std::endl;
                        }
                    }
                }
            } while (FindNextFileW(hFind, &findFileData));
            FindClose(hFind);
        }

        std::cout << "Total loaded configs: " << programs.size() << std::endl;
    }

    // 调用游戏控制器
    bool CallGameController(const ProgramConfig& config) {
        // 直接使用现有的JSON配置文件
        std::wstring configFilePath = configFolderPath + L"\\" + config.name + L".json";

        // 检查配置文件是否存在
        if (GetFileAttributesW(configFilePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::cout << "Config file not found: " << WStringToUTF8(configFilePath) << std::endl;
            return false;
        }

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
            CREATE_NEW_CONSOLE,
            NULL,
            NULL,
            &si,
            &pi
        );

        if (success) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return true;
        }
        else {
            DWORD error = GetLastError();
            std::cerr << "Failed to call game controller, error code: " << error << std::endl;
            return false;
        }
    }

    // 显示函数
    void DisplayProgramInfo(const ProgramConfig& config, size_t index, size_t total) {
        std::cout << index + 1 << ". [Order:" << config.order << "] ";
        PrintWString(config.name);
        std::cout << std::endl;

        std::cout << "   Description: ";
        PrintWString(config.description);
        std::cout << std::endl;

        std::cout << "   Path: ";
        PrintWString(config.path);
        std::cout << std::endl;

        if (config.type == ProgramType::ExeWithArgument && !config.arguments.empty()) {
            std::cout << "   Arguments (" << config.arguments.size() << "): ";
            for (size_t j = 0; j < config.arguments.size() && j < 5; j++) {
                if (j > 0) std::cout << ", ";
                PrintWString(config.arguments[j]);
            }
            std::cout << std::endl;
        }

        if (config.killAfterSeconds > 0 && !config.processNameToKill.empty()) {
            std::cout << "   Auto-close: Close ";
            PrintWString(config.processNameToKill);
            std::cout << " after " << config.killAfterSeconds << " seconds" << std::endl;
        }

        std::cout << "   Delay after start: " << config.delayAfterStart << " ms" << std::endl;
        std::cout << std::endl;
    }

    void DisplayStartupInfo(const ProgramConfig& config, size_t index, size_t total) {
        std::cout << "[" << GetCurrentTime() << "] Preparing to launch (" << index + 1 << "/" << total
            << "): [Order:" << config.order << "] ";
        PrintWString(config.name);
        std::cout << std::endl;

        std::cout << "Description: ";
        PrintWString(config.description);
        std::cout << std::endl;

        if (config.killAfterSeconds > 0 && !config.processNameToKill.empty()) {
            std::cout << "Auto-close: Close ";
            PrintWString(config.processNameToKill);
            std::cout << " after " << config.killAfterSeconds << " seconds" << std::endl;
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
        if (!InitializeConfigFolder()) {
            std::cout << "Failed to initialize program, press any key to exit..." << std::endl;
            std::cin.get();
            exit(1);
        }
    }

    // 设置自定义名称的方法
    void SetGameControllerName(const std::wstring& name) {
        gameControllerName = name;
        // 重新初始化路径
        wchar_t exePath[MAX_PATH];
        if (GetModuleFileNameW(NULL, exePath, MAX_PATH) != 0) {
            std::wstring exeDir = exePath;
            size_t lastSlash = exeDir.find_last_of(L"\\/");
            if (lastSlash != std::wstring::npos) {
                exeDir = exeDir.substr(0, lastSlash);
                gameControllerPath = exeDir + L"\\" + gameControllerName;
            }
        }
    }

    void SetConfigFolderName(const std::wstring& name) {
        configFolderName = name;
        // 重新初始化配置文件夹路径
        wchar_t exePath[MAX_PATH];
        if (GetModuleFileNameW(NULL, exePath, MAX_PATH) != 0) {
            std::wstring exeDir = exePath;
            size_t lastSlash = exeDir.find_last_of(L"\\/");
            if (lastSlash != std::wstring::npos) {
                exeDir = exeDir.substr(0, lastSlash);
                configFolderPath = exeDir + L"\\" + configFolderName;

                DWORD attr = GetFileAttributesW(configFolderPath.c_str());
                if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                    if (CreateDirectoryW(configFolderPath.c_str(), NULL)) {
                        std::wcout << L"Created config folder: " << configFolderPath << std::endl;
                    }
                    else {
                        std::wcout << L"Failed to create config folder: " << configFolderPath << std::endl;
                    }
                }
            }
        }
    }

    void SetConsoleUTF8() {
        SetConsoleOutputCP(65001);
        SetConsoleCP(65001);
    }

    void InitializePrograms() {
        std::cout << "Initializing program configurations..." << std::endl;

        // 清空现有程序列表
        programs.clear();

        // 从文件夹加载配置
        LoadConfigsFromFolder();

        std::cout << "Initialization completed, loaded " << programs.size() << " configs" << std::endl;
    }

    void Run() {
        SetConsoleUTF8();
        SetConsoleTitleW(L"Game Launcher - Main Controller");

        std::cout << "Game Launcher v4.0 - Distributed Control Edition" << std::endl;
        std::cout << "================================================" << std::endl;

        // 显示自定义设置信息
        std::cout << "Game Controller: " << WStringToUTF8(gameControllerName) << std::endl;
        std::cout << "Config Folder: " << WStringToUTF8(configFolderName) << std::endl;
        std::cout << "Full Path: " << WStringToUTF8(configFolderPath) << std::endl << std::endl;

        // 检查游戏控制器是否存在
        if (GetFileAttributesW(gameControllerPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::cout << "Error: Cannot find " << WStringToUTF8(gameControllerName) << ", please ensure it's in the same directory as the main program." << std::endl;
            std::cout << "Press any key to exit..." << std::endl;
            std::cin.get();
            return;
        }

        // 过滤和排序程序
        auto enabledPrograms = programs;
        enabledPrograms.erase(std::remove_if(enabledPrograms.begin(), enabledPrograms.end(),
            [](const ProgramConfig& c) { return !c.enabled; }), enabledPrograms.end());

        std::sort(enabledPrograms.begin(), enabledPrograms.end(),
            [](const ProgramConfig& a, const ProgramConfig& b) { return a.order < b.order; });

        // 显示配置信息
        std::cout << "Currently enabled program configurations (" << enabledPrograms.size() << "):" << std::endl;
        std::cout << "================================================" << std::endl;

        for (size_t i = 0; i < enabledPrograms.size(); i++) {
            DisplayProgramInfo(enabledPrograms[i], i, enabledPrograms.size());
        }

        // 如果没有启用的程序，提示用户
        if (enabledPrograms.empty()) {
            std::cout << "No enabled program configurations." << std::endl;
            std::cout << "Please edit config files and set 'enabled' to true." << std::endl;
            std::cout << "Press any key to exit..." << std::endl;
            std::cin.get();
            return;
        }

        // 启动程序
        std::cout << "================================================" << std::endl;
        std::cout << "Starting programs..." << std::endl << std::endl;

        for (size_t i = 0; i < enabledPrograms.size(); i++) {
            const auto& config = enabledPrograms[i];
            DisplayStartupInfo(config, i, enabledPrograms.size());

            if (CallGameController(config)) {
                std::cout << "Started: ";
                PrintWString(config.name);
                std::cout << std::endl;
            }
            else {
                std::cout << "Failed to start: ";
                PrintWString(config.name);
                std::cout << std::endl;
            }

            if (i < enabledPrograms.size() - 1) {
                int delaySeconds = config.delayAfterStart / 1000;
                std::cout << "Waiting " << delaySeconds << " seconds before starting next program..." << std::endl << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(config.delayAfterStart));
            }
        }

        std::cout << "================================================" << std::endl;
        std::cout << "All programs started successfully!" << std::endl;
        std::cout << "Press any key to exit..." << std::endl;
        std::cin.get();
    }
};

int main() {
    ProgramLauncher launcher;

    // 在这里可以自定义名称（可选）
    // launcher.SetGameControllerName(L"GameMJ_Controller.exe");
    // launcher.SetConfigFolderName(L"MyConfigs");

    launcher.InitializePrograms();
    launcher.Run();
    return 0;
}