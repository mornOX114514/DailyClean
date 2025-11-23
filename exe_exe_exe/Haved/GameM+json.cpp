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

// 程序类型枚举
enum class ProgramType {
    Exe,           // 普通EXE程序
    Bat,           // BAT脚本文件
    ExeWithJson    // 需要JSON参数的EXE程序
};

// 程序配置类
struct ProgramConfig {
    int order;              // 执行顺序（唯一，从小到大依次执行）
    bool enabled;           // 是否启用该程序
    std::wstring path;      // 程序路径（使用宽字符）
    std::wstring arguments; // 命令行参数
    ProgramType type;       // 程序类型
    std::wstring description;// 描述信息（使用宽字符）
    int delayAfterStart;    // 启动后等待时间（毫秒）
    
    // 新增：自动关闭功能
    std::wstring processNameToKill; // 要关闭的进程名（如：MAA.exe）
    int killAfterSeconds;           // 启动后多少秒关闭（0表示不关闭）

    ProgramConfig(int ord, bool en, const std::wstring& p, const std::wstring& args, 
                  ProgramType t, const std::wstring& desc, int delay,
                  const std::wstring& killProcess = L"", int killAfter = 0)
        : order(ord), enabled(en), path(p), arguments(args), type(t), 
          description(desc), delayAfterStart(delay), 
          processNameToKill(killProcess), killAfterSeconds(killAfter) {}
};

class ProgramLauncher {
private:
    std::vector<ProgramConfig> programs;
    std::vector<HANDLE> processHandles; // 保存进程句柄用于后续关闭

public:
    // 设置控制台编码为UTF-8
    void SetConsoleUTF8() {
        SetConsoleOutputCP(65001); // UTF-8
        SetConsoleCP(65001);       // UTF-8
    }

    // 初始化程序配置
    void InitializePrograms() {
        // === 在这里配置程序路径、参数和启动顺序 ===
        programs = {
            // 第一个程序 - 30秒后自动关闭
            ProgramConfig(10, false, LR"(D:\MAA\MAA.exe)", L"", 
                         ProgramType::Exe, L"MAA程序", 2000,
                         L"MAA.exe", 30), // 30秒后关闭MAA.exe
            
            // 第二个程序 - 需要JSON参数，45秒后自动关闭
            ProgramConfig(20, false, LR"(D:\BAAH1.9.0\BAAH.exe)", LR"(Daily.json)", 
                         ProgramType::ExeWithJson, L"BAAH程序", 3000,
                         L"BAAH.exe", 45), // 45秒后关闭BAAH.exe
            
            // 第三个程序 - 不自动关闭
            ProgramConfig(30, false, LR"(D:\starrailMAA\March7thAssistant_v2.7.0_full\March7th Assistant.exe)", L"", 
                         ProgramType::Exe, L"星穹铁道助手", 1500),
            
            // 第四个程序 - BAT文件，60秒后自动关闭cmd进程
            ProgramConfig(40, false, LR"(D:\genshin_MAA\Daily_GI.bat)", L"", 
                         ProgramType::Bat, L"原神日常脚本", 2500,
                         L"cmd.exe", 60) // 60秒后关闭cmd.exe
        };
    }

    // 根据进程名查找并关闭进程
    bool KillProcessByName(const std::wstring& processName) {
        bool found = false;
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) {
            return false;
        }

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

    // 异步关闭进程的线程函数
    static void KillProcessAfterDelay(ProgramLauncher* launcher, const ProgramConfig& config, int delayMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        
        std::wcout << L"[" << GetCurrentTimeString() << L"] 正在关闭进程: " << config.processNameToKill << std::endl;
        if (!launcher->KillProcessByName(config.processNameToKill)) {
            std::wcout << L"✗ 未找到进程: " << config.processNameToKill << std::endl;
        }
    }

    // 获取当前时间字符串（用于线程）
    static std::wstring GetCurrentTimeString() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buffer[64];
        swprintf_s(buffer, sizeof(buffer)/sizeof(wchar_t), L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
        return std::wstring(buffer);
    }

    // 检查文件是否存在
    bool FileExists(const std::wstring& path) {
        DWORD attrib = GetFileAttributesW(path.c_str());
        return (attrib != INVALID_FILE_ATTRIBUTES && !(attrib & FILE_ATTRIBUTE_DIRECTORY));
    }

    // 宽字符串转多字节字符串（用于输出）
    std::string WStringToString(const std::wstring& wstr) {
        if (wstr.empty()) return "";
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string str(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], size_needed, NULL, NULL);
        return str;
    }

    // 启动程序
    bool StartProgram(const ProgramConfig& config) {
        try {
            STARTUPINFOW si = { sizeof(si) };
            PROCESS_INFORMATION pi;
            std::wstring commandLine;

            switch (config.type) {
                case ProgramType::Bat:
                    // 对于BAT文件，使用cmd.exe来执行
                    commandLine = L"cmd.exe /c \"" + config.path + L"\"";
                    break;

                case ProgramType::Exe:
                    // 对于普通EXE文件，直接执行
                    commandLine = config.path;
                    break;

                case ProgramType::ExeWithJson:
                    // 对于需要JSON参数的EXE，格式为: program.exe config.json
                    commandLine = config.path;
                    if (!config.arguments.empty()) {
                        commandLine += L" " + config.arguments;
                    }
                    break;
            }

            // 获取工作目录
            std::wstring workingDirectory = config.path.substr(0, config.path.find_last_of(L"\\/"));

            // 创建进程
            BOOL success = CreateProcessW(
                NULL,                           // 不指定可执行文件名称
                const_cast<LPWSTR>(commandLine.c_str()), // 命令行
                NULL,                           // 进程句柄不可继承
                NULL,                           // 线程句柄不可继承
                FALSE,                          // 不继承句柄
                0,                              // 无创建标志
                NULL,                           // 使用父进程环境块
                workingDirectory.empty() ? NULL : workingDirectory.c_str(), // 工作目录
                &si,                            // STARTUPINFO
                &pi                             // PROCESS_INFORMATION
            );

            if (success) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                
                // 如果配置了自动关闭，启动关闭线程
                if (config.killAfterSeconds > 0 && !config.processNameToKill.empty()) {
                    int delayMs = config.killAfterSeconds * 1000;
                    std::thread killThread(KillProcessAfterDelay, this, config, delayMs);
                    killThread.detach(); // 分离线程，让它独立运行
                }
                
                return true;
            } else {
                std::cerr << "CreateProcess失败，错误代码: " << GetLastError() << std::endl;
                return false;
            }
        } catch (...) {
            return false;
        }
    }

    // 获取完整命令
    std::wstring GetFullCommand(const ProgramConfig& config) {
        switch (config.type) {
            case ProgramType::Bat:
                return L"cmd.exe /c \"" + config.path + L"\"";
            
            case ProgramType::Exe:
                return config.path;
            
            case ProgramType::ExeWithJson:
                if (config.arguments.empty()) {
                    return config.path;
                } else {
                    return config.path + L" " + config.arguments;
                }
            
            default:
                return config.path;
        }
    }

    // 显示当前时间
    std::string GetCurrentTime() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char buffer[64];
        sprintf_s(buffer, sizeof(buffer), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
        return std::string(buffer);
    }

    // 输出宽字符串（支持中文）
    void PrintWString(const std::wstring& wstr) {
        std::string str = WStringToString(wstr);
        std::cout << str;
    }

    // 运行启动器
    void Run() {
        // 设置控制台编码
        SetConsoleUTF8();
        SetConsoleTitleW(L"游戏助手启动器 - 自动关闭版");

        std::cout << "游戏助手启动器 v2.0 - 自动关闭版" << std::endl;
        std::cout << "功能特性:" << std::endl;
        std::cout << "  ";
        PrintWString(L"✓ 通过Order数值控制执行顺序（从小到大）");
        std::cout << std::endl;
        std::cout << "  ";
        PrintWString(L"✓ 通过Enabled开关控制程序是否执行");
        std::cout << std::endl;
        std::cout << "  ";
        PrintWString(L"✓ 可分别设置每个程序的启动间隔");
        std::cout << std::endl;
        std::cout << "  ";
        PrintWString(L"✓ 支持自动关闭指定进程");
        std::cout << std::endl;
        std::cout << "  ";
        PrintWString(L"✓ 支持EXE、BAT和带JSON参数的EXE");
        std::cout << std::endl;
        std::cout << std::endl;

        // 按Order排序并过滤启用的程序
        std::vector<ProgramConfig> enabledPrograms;
        std::vector<ProgramConfig> disabledPrograms;

        for (const auto& program : programs) {
            if (program.enabled) {
                enabledPrograms.push_back(program);
            } else {
                disabledPrograms.push_back(program);
            }
        }

        // 按Order排序
        std::sort(enabledPrograms.begin(), enabledPrograms.end(), 
                  [](const ProgramConfig& a, const ProgramConfig& b) { return a.order < b.order; });
        
        std::sort(disabledPrograms.begin(), disabledPrograms.end(), 
                  [](const ProgramConfig& a, const ProgramConfig& b) { return a.order < b.order; });

        std::cout << "当前启用的程序启动顺序和间隔配置:" << std::endl;
        std::cout << "=====================================" << std::endl;
        
        for (size_t i = 0; i < enabledPrograms.size(); i++) {
            const auto& config = enabledPrograms[i];
            std::cout << i + 1 << ". [Order:" << config.order << "] ";
            PrintWString(config.description);
            std::cout << std::endl;
            
            std::cout << "   路径: ";
            PrintWString(config.path);
            std::cout << std::endl;
            
            if (config.type == ProgramType::ExeWithJson && !config.arguments.empty()) {
                std::cout << "   参数: ";
                PrintWString(config.arguments);
                std::cout << std::endl;
            }
            
            if (config.killAfterSeconds > 0 && !config.processNameToKill.empty()) {
                std::cout << "   自动关闭: " << config.killAfterSeconds << "秒后关闭 ";
                PrintWString(config.processNameToKill);
                std::cout << std::endl;
            } else {
                std::cout << "   自动关闭: 无" << std::endl;
            }
            
            std::cout << "   启动后等待: " << config.delayAfterStart << " 毫秒" << std::endl;
            std::cout << "   状态: " << (config.enabled ? "启用" : "禁用") << std::endl;
            std::cout << std::endl;
        }

        // 显示被禁用的程序
        if (!disabledPrograms.empty()) {
            std::cout << "已禁用的程序:" << std::endl;
            std::cout << "-------------------------------------" << std::endl;
            for (const auto& config : disabledPrograms) {
                std::cout << "[Order:" << config.order << "] ";
                PrintWString(config.description);
                std::cout << " - 已禁用" << std::endl;
            }
            std::cout << std::endl;
        }

        std::cout << "=====================================" << std::endl;
        std::cout << "开始启动程序..." << std::endl;
        std::cout << "共有 " << enabledPrograms.size() << " 个程序需要启动（" << disabledPrograms.size() << " 个已禁用）" << std::endl;
        std::cout << std::endl;

        for (size_t i = 0; i < enabledPrograms.size(); i++) {
            const auto& config = enabledPrograms[i];
            std::cout << "[" << GetCurrentTime() << "] 准备启动 (" << i + 1 << "/" << enabledPrograms.size() 
                      << "): [Order:" << config.order << "] ";
            PrintWString(config.description);
            std::cout << std::endl;
            
            std::cout << "路径: ";
            PrintWString(config.path);
            std::cout << std::endl;
            
            // 显示完整的启动命令
            std::wstring fullCommand = GetFullCommand(config);
            std::cout << "命令: ";
            PrintWString(fullCommand);
            std::cout << std::endl;

            // 显示自动关闭信息
            if (config.killAfterSeconds > 0 && !config.processNameToKill.empty()) {
                std::cout << "自动关闭: " << config.killAfterSeconds << "秒后关闭 ";
                PrintWString(config.processNameToKill);
                std::cout << std::endl;
            }

            // 检查文件是否存在（BAT文件不检查）
            if (config.type != ProgramType::Bat && !FileExists(config.path)) {
                std::cout << "✗ 文件不存在: ";
                PrintWString(config.path);
                std::cout << std::endl;
                continue;
            }

            bool success = StartProgram(config);
            if (success) {
                std::cout << "✓ 成功启动: ";
                PrintWString(config.description);
                std::cout << std::endl;
            } else {
                std::cout << "✗ 启动失败: ";
                PrintWString(config.description);
                std::cout << std::endl;
            }

            // 如果不是最后一个程序，等待指定间隔
            if (i < enabledPrograms.size() - 1) {
                int delaySeconds = config.delayAfterStart / 1000;
                std::cout << "等待 " << delaySeconds << " 秒后启动下一个程序..." << std::endl;
                std::cout << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(config.delayAfterStart));
            }
        }

        std::cout << "=====================================" << std::endl;
        std::cout << "所有程序启动完成！" << std::endl;
        std::cout << "自动关闭任务已在后台运行..." << std::endl;
        std::cout << "按任意键退出..." << std::endl;
        std::cin.get();
    }
};

int main() {
    ProgramLauncher launcher;
    
    // 初始化程序配置
    launcher.InitializePrograms();
    
    // 运行启动器
    launcher.Run();
    
    return 0;
}