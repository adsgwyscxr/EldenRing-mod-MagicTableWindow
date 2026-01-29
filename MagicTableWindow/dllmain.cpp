/*
1.头文件和一些变量声明
2.****导入ini设置****
3.****模拟按键****
    这里模拟↑↓←→有误，会模拟成小键盘8、2、4、6，其他一些不常规按键没测试，可能也有问题
4.****游戏内存读取和修改****
？获取游戏窗口句柄
5.****GDI+内存绘画****
6.****创建子窗口****
    窗口过程函数的WM_MOUSEMOVE里对鼠标位置进行检测，并让鼠标所在部分背景高亮
7.****线程****
    用于初始化和按键检测


1-5都是定义函数，调用在6和7
2-4是AI写的（咱也是小白才开始学，写的不好还请见谅）




*/

// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"
#include <strsafe.h>
#include <objidl.h>//不写这行，#include <gdiplus.h>会有2000+报错
#include <gdiplus.h>
#include <thread>
#include <iostream>
#include <tlhelp32.h>  // 用于64位模块枚举
#include <string>
#include <locale>
#include <codecvt>
#pragma comment(lib, "Gdiplus.lib")
using namespace Gdiplus;

GdiplusStartupInput gdiplusStartupInput;
ULONG_PTR gdiplusToken;                         //初始化GDI+ （1/2）



HWND g_hGameWindow = NULL;       //定义全局变量g_hGameWindow存储游戏窗口的句柄
HWND MagicTableWindow = NULL;    //定义全局变量MagicTableWindow存储自建窗口的句柄
HINSTANCE g_hInstance = NULL;    //dll句柄
//GDI+内存画布
Bitmap* g_MTWbgBmp = nullptr;       //bg-背景
Graphics* g_MTWbgGph = nullptr;
Bitmap* g_MTWpngBmp = nullptr;      //png-图片
Graphics* g_MTWpngGph = nullptr;
Bitmap* g_MTWshowBmp = nullptr;     //show-显示
Graphics* g_MTWshowGph = nullptr;

Graphics* g_MTWscreenGph = nullptr; //screen-窗口画刷
int gameWidth;
int gameHeight;
int MTWwidth;
int MTWheight;
int MTWx;
int MTWy;
uint8_t nowMagic = 0;       //使用字节类型
const float MTW_WIDTH_RATIO = 0.84f;     //用于计算MTW窗口的比例
const float MTW_HEIGHT_RATIO = 0.24f;
const BYTE MTW_ALPHA = 224; // 窗口透明度，128为50% 透明度
DWORD oldg_M1, oldg_M2, oldg_M3, oldg_M4, oldg_M5, oldg_M6, oldg_M7, oldg_M8, oldg_M9, oldg_M10, oldg_M11, oldg_M12, oldg_M13, oldg_M14;
DWORD g_M1, g_M2, g_M3, g_M4, g_M5, g_M6, g_M7, g_M8, g_M9, g_M10, g_M11, g_M12, g_M13, g_M14;
UINT64 g_magicSlotAddress = 0;  // 新建全局变量保存魔法槽地址
volatile bool g_shouldExit = false;         //线程退出标志
bool isTABPressdLastTime = false;    //记录tab键的上一次状态
bool isSprint = false;    //记录跑步状态
bool isSprintMode = false;    //记录疾跑模式状态（Z键切换的模式）
bool disableTab = false;        //当鼠标左键时，不检测松开tab键的魔法菜单关闭
//键盘钩子
HHOOK g_hLowLevelKeyboardHook = NULL;
bool g_simulatingKey = false;
DWORD g_currentSimulatedKey = 0;


//*********************************************导入ini设置*****************************************************************
// 配置结构体
struct GameConfig {
    //界面
    int windowSize = 12;
    int windowPosition = 32;

    // 游戏按键
    DWORD rollKey = 'O';
    DWORD switchMagicKey = 'I';
    DWORD switchItemKey = 'K';
    DWORD switchRightWeaponKey = 'J';
    DWORD switchLeftWeaponKey = 'L';
    DWORD interactKey = 'E';
    DWORD mapKey = 'M';

    // Mod按键
    DWORD quickSlot1Key = '1';
    DWORD quickSlot2Key = '2';
    DWORD quickSlot3Key = '3';
    DWORD quickSlot4Key = '4';
    DWORD memoryMenuKey = 'N';
    DWORD magicTableKey = VK_TAB;
    DWORD modRollKey = VK_SHIFT;
    DWORD sprintToggleKey = 'Z';

    // 功能开关
    bool enableMemoryMenu = true;
    bool enableMagicTable = true;
    bool enableNumpadItems = true;
    bool enableQuickSlots = true;
    bool enableRollSprint = true;
} g_config;


// 宽字符串转字符串
std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// 字符串转虚拟键码
DWORD StringToVirtualKey(const std::wstring& keyStr) {
    if (keyStr.empty()) return 0;

    // 去除可能的空白字符
    std::wstring trimmedStr = keyStr;
    while (!trimmedStr.empty() && (trimmedStr.back() == L' ' || trimmedStr.back() == L'\t' || trimmedStr.back() == L'\r' || trimmedStr.back() == L'\n')) {
        trimmedStr.pop_back();
    }
    while (!trimmedStr.empty() && (trimmedStr.front() == L' ' || trimmedStr.front() == L'\t')) {
        trimmedStr.erase(0, 1);
    }

    if (trimmedStr.empty()) return 0;

    // 单字符按键
    if (trimmedStr.length() == 1) {
        wchar_t c = trimmedStr[0];
        if (c >= L'A' && c <= L'Z') return c;
        if (c >= L'0' && c <= L'9') return c;
        return 0;
    }

    // 特殊按键映射 - 使用 _wcsicmp 进行不区分大小写比较
    if (_wcsicmp(trimmedStr.c_str(), L"VK_UP") == 0) return VK_UP;
    if (_wcsicmp(trimmedStr.c_str(), L"VK_DOWN") == 0) return VK_DOWN;
    if (_wcsicmp(trimmedStr.c_str(), L"VK_LEFT") == 0) return VK_LEFT;
    if (_wcsicmp(trimmedStr.c_str(), L"VK_RIGHT") == 0) return VK_RIGHT;
    if (_wcsicmp(trimmedStr.c_str(), L"VK_SHIFT") == 0) return VK_SHIFT;
    if (_wcsicmp(trimmedStr.c_str(), L"VK_TAB") == 0) return VK_TAB;
    if (_wcsicmp(trimmedStr.c_str(), L"VK_ESCAPE") == 0) return VK_ESCAPE;
    if (_wcsicmp(trimmedStr.c_str(), L"VK_SPACE") == 0) return VK_SPACE;
    if (_wcsicmp(trimmedStr.c_str(), L"VK_RETURN") == 0) return VK_RETURN;
    if (_wcsicmp(trimmedStr.c_str(), L"VK_CONTROL") == 0) return VK_CONTROL;
    if (_wcsicmp(trimmedStr.c_str(), L"VK_ALT") == 0) return VK_MENU;
    if (_wcsicmp(trimmedStr.c_str(), L"VK_LSHIFT") == 0) return VK_LSHIFT;
    if (_wcsicmp(trimmedStr.c_str(), L"VK_RSHIFT") == 0) return VK_RSHIFT;
    if (_wcsicmp(trimmedStr.c_str(), L"VK_LCONTROL") == 0) return VK_LCONTROL;
    if (_wcsicmp(trimmedStr.c_str(), L"VK_RCONTROL") == 0) return VK_RCONTROL;



    return 0;
}

// 读取配置文件
void LoadConfig() {
    // 获取DLL所在目录
    HMODULE hModule = g_hInstance;
    wchar_t dllPath[MAX_PATH];
    GetModuleFileName(hModule, dllPath, MAX_PATH);

    // 提取目录路径
    wchar_t* lastSlash = wcsrchr(dllPath, L'\\');
    if (lastSlash) {
        *(lastSlash + 1) = L'\0';
    }

    // 构建配置文件路径
    wchar_t configPath[MAX_PATH];
    wcscpy_s(configPath, dllPath);
    wcscat_s(configPath, L"setting.ini");

    // 读取游戏按键配置
    wchar_t buffer[256];

    //界面
    g_config.windowSize = GetPrivateProfileInt(L"Display", L"windowSize", 12, configPath);
    g_config.windowPosition = GetPrivateProfileInt(L"Display", L"windowPosition", 32, configPath);

    // GameKeys部分
    GetPrivateProfileString(L"GameKeys", L"Roll", L"O", buffer, 256, configPath);
    g_config.rollKey = StringToVirtualKey(buffer);

    GetPrivateProfileString(L"GameKeys", L"SwitchMagic", L"VK_UP", buffer, 256, configPath);
    g_config.switchMagicKey = StringToVirtualKey(buffer);

    GetPrivateProfileString(L"GameKeys", L"SwitchItem", L"VK_DOWN", buffer, 256, configPath);
    g_config.switchItemKey = StringToVirtualKey(buffer);

    GetPrivateProfileString(L"GameKeys", L"SwitchRightWeapon", L"VK_RIGHT", buffer, 256, configPath);
    g_config.switchRightWeaponKey = StringToVirtualKey(buffer);

    GetPrivateProfileString(L"GameKeys", L"SwitchLeftWeapon", L"VK_LEFT", buffer, 256, configPath);
    g_config.switchLeftWeaponKey = StringToVirtualKey(buffer);

    GetPrivateProfileString(L"GameKeys", L"Interact", L"E", buffer, 256, configPath);
    g_config.interactKey = StringToVirtualKey(buffer);

    GetPrivateProfileString(L"GameKeys", L"Map", L"M", buffer, 256, configPath);
    g_config.mapKey = StringToVirtualKey(buffer);

    // ModKeys 部分
    GetPrivateProfileString(L"ModKeys", L"QuickSlot1", L"1", buffer, 256, configPath);
    g_config.quickSlot1Key = StringToVirtualKey(buffer);

    GetPrivateProfileString(L"ModKeys", L"QuickSlot2", L"2", buffer, 256, configPath);
    g_config.quickSlot2Key = StringToVirtualKey(buffer);

    GetPrivateProfileString(L"ModKeys", L"QuickSlot3", L"3", buffer, 256, configPath);
    g_config.quickSlot3Key = StringToVirtualKey(buffer);

    GetPrivateProfileString(L"ModKeys", L"QuickSlot4", L"4", buffer, 256, configPath);
    g_config.quickSlot4Key = StringToVirtualKey(buffer);

    GetPrivateProfileString(L"ModKeys", L"MemoryMenu", L"N", buffer, 256, configPath);
    g_config.memoryMenuKey = StringToVirtualKey(buffer);

    GetPrivateProfileString(L"ModKeys", L"MagicTable", L"VK_TAB", buffer, 256, configPath);
    g_config.magicTableKey = StringToVirtualKey(buffer);

    GetPrivateProfileString(L"ModKeys", L"ModRoll", L"VK_SHIFT", buffer, 256, configPath);
    g_config.modRollKey = StringToVirtualKey(buffer);

    GetPrivateProfileString(L"ModKeys", L"SprintToggle", L"Z", buffer, 256, configPath);
    g_config.sprintToggleKey = StringToVirtualKey(buffer);

    // Features 部分
    g_config.enableMemoryMenu = GetPrivateProfileInt(L"Features", L"EnableMemoryMenu", 1, configPath) != 0;
    g_config.enableMagicTable = GetPrivateProfileInt(L"Features", L"EnableMagicTable", 1, configPath) != 0;
    g_config.enableNumpadItems = GetPrivateProfileInt(L"Features", L"EnableNumpadItems", 1, configPath) != 0;
    g_config.enableQuickSlots = GetPrivateProfileInt(L"Features", L"EnableQuickSlots", 1, configPath) != 0;
    g_config.enableRollSprint = GetPrivateProfileInt(L"Features", L"EnableRollSprint", 1, configPath) != 0;
}

//*************************************************模拟按键**********************************************************************
// 底层键盘Hook - 这个可以欺骗DirectInput
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    /*
    if (nCode >= 0) {                                               游戏会截断按键信息，焦点在游戏窗口时，信息传不到这里，所以现在用的GetAsyncKeyState来检测
        KBDLLHOOKSTRUCT* pKbStruct = (KBDLLHOOKSTRUCT*)lParam;
        if (wParam == WM_KEYDOWN && pKbStruct->vkCode == 'B') {
            MessageBoxW(NULL, L"检测到B键！", L"错误", MB_ICONERROR);

        }
        // 如果正在模拟按键，让它通过
        if (g_simulatingKey && pKbStruct->vkCode == g_currentSimulatedKey) {
            return CallNextHookEx(g_hLowLevelKeyboardHook, nCode, wParam, lParam);
        }
    }*/

    return CallNextHookEx(g_hLowLevelKeyboardHook, nCode, wParam, lParam);
}

// 安装底层Hook
bool InstallLowLevelHook() {
    g_hLowLevelKeyboardHook = SetWindowsHookEx(
        WH_KEYBOARD_LL,
        LowLevelKeyboardProc,
        g_hInstance, // 使用你的DLL句柄
        0
    );

    return g_hLowLevelKeyboardHook != NULL;
}

// 卸载Hook
void UninstallLowLevelHook() {
    if (g_hLowLevelKeyboardHook) {
        UnhookWindowsHookEx(g_hLowLevelKeyboardHook);
        g_hLowLevelKeyboardHook = NULL;
    }
}
// 模拟真实按键 - 异步版本
void SimulateRealKeyPress(DWORD vKey, DWORD duration = 17) {                //调用SimulateRealKeyPress()的传入参数：（vKey-虚拟键码，duration-按住几毫秒，不写就是默认17ms。游戏60帧，这里一帧多一点让游戏识别到按键）
    // 在新线程中执行，立即返回，不阻塞调用者
    std::thread([vKey, duration]() {
        g_simulatingKey = true;
        g_currentSimulatedKey = vKey;
        // 使用扫描码
        DWORD scanCode = MapVirtualKey(vKey, MAPVK_VK_TO_VSC);
        INPUT inputs[2] = {};
        // 按下
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wScan = scanCode;              //这里用扫描码，虚拟键码不会用
        inputs[0].ki.dwFlags = KEYEVENTF_SCANCODE;
        // 释放
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wScan = scanCode;
        inputs[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        // 发送按键事件
        if (vKey == g_config.rollKey && isSprintMode && isSprint) {
            //在疾跑模式下按翻滚键：先释放再按下再释放
            SendInput(1, &inputs[1], sizeof(INPUT)); // 释放翻滚键
            Sleep(duration);
            SendInput(1, &inputs[0], sizeof(INPUT)); // 按下翻滚键（翻滚）
            Sleep(duration);
            SendInput(1, &inputs[1], sizeof(INPUT)); // 释放翻滚键
            Sleep(duration);
            SendInput(1, &inputs[0], sizeof(INPUT)); // 重新按下翻滚键（恢复疾跑）
        }
        else {
            SendInput(1, &inputs[0], sizeof(INPUT)); //按下
            Sleep(duration);      //在后台线程中Sleep
            SendInput(1, &inputs[1], sizeof(INPUT)); //释放
        }
        g_simulatingKey = false;
        }).detach();  // detach让线程独立运行
}
// 随身包包 
void QuickSlotsKeyPress(DWORD vKey, DWORD duration = 17) {
    // 在新线程中执行，立即返回，不阻塞调用者
    std::thread([vKey, duration]() {
        // 使用扫描码
        DWORD scanCode1 = MapVirtualKey(g_config.interactKey, MAPVK_VK_TO_VSC);
        DWORD scanCode2 = MapVirtualKey(vKey, MAPVK_VK_TO_VSC);
        INPUT inputs[4] = {};
        // 按下
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wScan = scanCode1;
        inputs[0].ki.dwFlags = KEYEVENTF_SCANCODE;
        // 释放
        inputs[3].type = INPUT_KEYBOARD;
        inputs[3].ki.wScan = scanCode1;
        inputs[3].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        // 按下
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wScan = scanCode2;
        inputs[1].ki.dwFlags = KEYEVENTF_SCANCODE;
        // 释放
        inputs[2].type = INPUT_KEYBOARD;
        inputs[2].ki.wScan = scanCode2;
        inputs[2].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        // 发送按键事件
            SendInput(1, &inputs[0], sizeof(INPUT)); //按下
            Sleep(30);      //在后台线程中Sleep
            SendInput(1, &inputs[1], sizeof(INPUT)); //按下
            Sleep(17);      //在后台线程中Sleep
            SendInput(1, &inputs[2], sizeof(INPUT)); //释放
            SendInput(1, &inputs[3], sizeof(INPUT)); //释放
        }).detach();  // detach让线程独立运行
}
// 疾跑模式切换
void ToggleSprintMode() {
    isSprintMode = !isSprintMode;

    if (isSprintMode) {
        // 切换到疾跑模式，自动开始疾跑
        if (!isSprint) {
            std::thread([]() {
                DWORD scanCode = MapVirtualKey(g_config.rollKey, MAPVK_VK_TO_VSC);
                INPUT input = {};
                input.type = INPUT_KEYBOARD;
                input.ki.wScan = scanCode;
                input.ki.dwFlags = KEYEVENTF_SCANCODE;
                SendInput(1, &input, sizeof(INPUT)); // 按下翻滚键开始疾跑
                isSprint = true;
                }).detach();
        }
    }
    else {
        // 切换到走路模式，停止疾跑
        if (isSprint) {
            std::thread([]() {
                DWORD scanCode = MapVirtualKey(g_config.rollKey, MAPVK_VK_TO_VSC);
                INPUT input = {};
                input.type = INPUT_KEYBOARD;
                input.ki.wScan = scanCode;
                input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
                SendInput(1, &input, sizeof(INPUT)); // 释放翻滚键停止疾跑
                isSprint = false;
                }).detach();
        }
    }
}

//**************************************************游戏内存读取和修改************************************************************
// 64位内存读取辅助函数
UINT64 ReadMemoryUINT64(HANDLE hProcess, UINT64 address) {
    UINT64 value = 0;
    SIZE_T bytesRead;
    ReadProcessMemory(hProcess, (LPCVOID)address, &value, sizeof(UINT64), &bytesRead);
    return value;
}

DWORD ReadMemoryDWORD(HANDLE hProcess, UINT64 address) {
    DWORD value = 0;
    SIZE_T bytesRead;
    ReadProcessMemory(hProcess, (LPCVOID)address, &value, sizeof(DWORD), &bytesRead);
    return value;
}

// 64位注入方式获取模块基址
UINT64 GetModuleBaseAddress64(DWORD processId, const wchar_t* moduleName) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    MODULEENTRY32W moduleEntry;
    moduleEntry.dwSize = sizeof(MODULEENTRY32W);

    if (Module32FirstW(hSnapshot, &moduleEntry)) {
        do {
            if (_wcsicmp(moduleEntry.szModule, moduleName) == 0) {
                CloseHandle(hSnapshot);
                return (UINT64)moduleEntry.modBaseAddr;
            }
        } while (Module32NextW(hSnapshot, &moduleEntry));
    }

    CloseHandle(hSnapshot);
    return 0;
}

// 64位主读取函数
bool ReadGameMemory() {
    // 获取进程句柄
    DWORD processId;
    GetWindowThreadProcessId(g_hGameWindow, &processId);
    HANDLE hProcess = OpenProcess(PROCESS_VM_READ, FALSE, processId);

    if (!hProcess) {
        return false;
    }

    // 使用64位注入方式获取模块基址
    UINT64 baseAddress = GetModuleBaseAddress64(processId, L"eldenring.exe");
    if (!baseAddress) {
        CloseHandle(hProcess);
        return false;
    }

    // 计算64位多级指针地址
    UINT64 targetAddress = baseAddress + 0x3D5DF38;
    UINT64 ptr1 = ReadMemoryUINT64(hProcess, targetAddress);
    if (!ptr1) { CloseHandle(hProcess); return false; }

    UINT64 ptr2 = ReadMemoryUINT64(hProcess, ptr1 + 0x08);
    if (!ptr2) { CloseHandle(hProcess); return false; }

    UINT64 ptr3 = ReadMemoryUINT64(hProcess, ptr2 + 0x530);
    if (!ptr3) { CloseHandle(hProcess); return false; }

    // 获取并保存魔法槽地址【eldenring.exe+3D5DF38指针一：+08指针二：+530指针三：+80】
    g_magicSlotAddress = ptr3 + 0x80;

    // 读取14个地址的值
    DWORD* globals[] = { &g_M1, &g_M2, &g_M3, &g_M4, &g_M5, &g_M6, &g_M7,
                       &g_M8, &g_M9, &g_M10, &g_M11, &g_M12, &g_M13, &g_M14 };

    for (int i = 0; i < 14; i++) {
        UINT64 offset = 0x10 + (i * 0x08);  // 0x10, 0x18, 0x20, 0x28...0x78
        *globals[i] = ReadMemoryDWORD(hProcess, ptr3 + offset);
    }

    CloseHandle(hProcess);
    return true;
}

// 修改魔法槽内存地址函数
bool WriteMagicSlot() {
    // 获取进程句柄，需要写入权限
    DWORD processId;
    GetWindowThreadProcessId(g_hGameWindow, &processId);
    HANDLE hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, processId);

    if (!hProcess) {
        return false;
    }

    // 重新获取地址，确保地址是最新的
    UINT64 baseAddress = GetModuleBaseAddress64(processId, L"eldenring.exe");
    if (!baseAddress) {
        CloseHandle(hProcess);
        return false;
    }

    // 重新计算多级指针地址
    UINT64 targetAddress = baseAddress + 0x3D5DF38;
    UINT64 ptr1 = ReadMemoryUINT64(hProcess, targetAddress);
    if (!ptr1) { CloseHandle(hProcess); return false; }

    UINT64 ptr2 = ReadMemoryUINT64(hProcess, ptr1 + 0x08);
    if (!ptr2) { CloseHandle(hProcess); return false; }

    UINT64 ptr3 = ReadMemoryUINT64(hProcess, ptr2 + 0x530);
    if (!ptr3) { CloseHandle(hProcess); return false; }

    UINT64 magicSlotAddr = ptr3 + 0x80;

    // 计算要写入的值：nowMagic-1（因为第一个魔法对应0，第二个对应1...）
    BYTE magicValue = (nowMagic > 0) ? (nowMagic - 1) : 0;

    // 写入内存
    SIZE_T bytesWritten;
    bool result = WriteProcessMemory(hProcess, (LPVOID)magicSlotAddr, &magicValue, sizeof(BYTE), &bytesWritten);

    CloseHandle(hProcess);
    return result && (bytesWritten == sizeof(BYTE));
}


// 新的内存修改函数 - 修改地址【eldenring.exe+3D5DF38指针一：+08指针二：+5D8】
bool WriteEquipItemAddress(BYTE value) {
    // 获取进程句柄，需要写入权限

    DWORD processId;
    GetWindowThreadProcessId(g_hGameWindow, &processId);
    HANDLE hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, processId);

    if (!hProcess) {
        return false;
    }

    // 获取基址
    UINT64 baseAddress = GetModuleBaseAddress64(processId, L"eldenring.exe");
    if (!baseAddress) {
        CloseHandle(hProcess);
        return false;
    }

    // 计算多级指针地址：eldenring.exe+3D5DF38 -> +08 -> +5D8
    UINT64 targetAddress = baseAddress + 0x3D5DF38;
    UINT64 ptr1 = ReadMemoryUINT64(hProcess, targetAddress);
    if (!ptr1) { CloseHandle(hProcess); return false; }

    UINT64 ptr2 = ReadMemoryUINT64(hProcess, ptr1 + 0x08);
    if (!ptr2) { CloseHandle(hProcess); return false; }

    UINT64 finalAddr = ptr2 + 0x5D8;

    // 写入字节值
    SIZE_T bytesWritten;
    bool result = WriteProcessMemory(hProcess, (LPVOID)finalAddr, &value, sizeof(BYTE), &bytesWritten);

    CloseHandle(hProcess);
    return result && (bytesWritten == sizeof(BYTE));
}



// 查找游戏窗口,获取游戏窗口句柄
HWND FindGameWindow() {                                         //FindWindow"W":W是Unicode宽字符，A是ANSI多字节。
    //比如CreateWindowExW、CreateWindowExA。
// 尝试查找《艾尔登法环》窗口
    HWND hWnd = FindWindowW(nullptr, L"ELDEN RING™");
    if (!hWnd) {
        hWnd = FindWindowW(nullptr, L"ELDEN RING");
    }
    if (!hWnd) {
        hWnd = FindWindowW(L"ELDENRING_WIN64", nullptr);
    }
    return hWnd;
}
//**************************************************GDI+内存绘画************************************************
/*画圆，好像是GDI
void DrawMTWRing(HWND hWnd) {
    HDC hdc = GetDC(hWnd);
    if (!hdc) {
        MessageBoxW(NULL, L"错误：无法获取hdc！", L"错误", MB_ICONERROR);
        return;
    }
    Graphics graphics(hdc);
    //内圆半径为高的0.25，圆环宽为高的0.2，;;;;;圆环宽减少一些，内圆半径增大一些，外框不变
    int penWidth = gameHeight * 0.15;
    int MTWRingX = gameWidth * 0.5 - (gameHeight * 0.4);       //圆环中心内切正方形的左上角点
    int MTWRingY = gameHeight * 0.1;
    int MTWRingR = gameHeight * 0.4;                           //内圆半径+圆环粗度*0.5

    Pen bluePen(Color(255, 0, 0, 255), penWidth);
    bluePen.SetAlignment(PenAlignmentCenter);
    graphics.DrawEllipse(&bluePen, MTWRingX, MTWRingY, 2* MTWRingR, 2* MTWRingR);

    ReleaseDC(hWnd, hdc);
}*/
void MTWdestroyBmps() {
    if (g_MTWbgGph) {
        delete g_MTWbgGph;
        g_MTWbgGph = nullptr;
    }
    if (g_MTWbgBmp) {
        delete g_MTWbgBmp;
        g_MTWbgBmp = nullptr;
    }
    if (g_MTWpngGph) {
        delete g_MTWpngGph;
        g_MTWpngGph = nullptr;
    }
    if (g_MTWpngBmp) {
        delete g_MTWpngBmp;
        g_MTWpngBmp = nullptr;
    }
    if (g_MTWshowGph) {
        delete g_MTWshowGph;
        g_MTWshowGph = nullptr;
    }
    if (g_MTWshowBmp) {
        delete g_MTWshowBmp;
        g_MTWshowBmp = nullptr;
    }
}
void MTWcreateBmps() {
    if (g_MTWbgGph) {
        MTWdestroyBmps();
    }
    g_MTWbgBmp = new Bitmap(MTWwidth, MTWheight);       //栈和堆的区别，new了就必须有delete
    g_MTWbgGph = new Graphics(g_MTWbgBmp);      //Bmp是图，Gph是画笔？，总之就是用Gph去画
    g_MTWpngBmp = new Bitmap(MTWwidth, MTWheight);
    g_MTWpngGph = new Graphics(g_MTWpngBmp);
    g_MTWshowBmp = new Bitmap(MTWwidth, MTWheight);
    g_MTWshowGph = new Graphics(g_MTWshowBmp);
}
void MTWdrawbg() {
    Pen pen1(Color(255, 70, 170, 240), 5.0f);       //pen1,不透明青蓝色
    SolidBrush brush1(Color(255, 10, 10, 15));      //brush1,不透明深黑色
    Rect rect(0, 0, MTWwidth, MTWheight);


    g_MTWbgGph->FillRectangle(&brush1, rect);
    g_MTWbgGph->DrawRectangle(&pen1, rect);
    g_MTWbgGph->DrawLine(&pen1, 0, MTWheight / 2, MTWwidth, MTWheight / 2);
    for (int i = 1; i < 7; i++) {
        int x = (MTWwidth * i) / 7;  // 分成7等份
        g_MTWbgGph->DrawLine(&pen1, x, 0, x, MTWheight);
    }
}
void MTWdrawImgs() {
    if (!g_MTWpngGph) return;
    // 创建全局变量数组指针，方便循环访问
    DWORD* magicValues[] = { &g_M1, &g_M2, &g_M3, &g_M4, &g_M5, &g_M6, &g_M7,
                           &g_M8, &g_M9, &g_M10, &g_M11, &g_M12, &g_M13, &g_M14 };
    // 先清空目标位图
    g_MTWpngGph->Clear(Color::Transparent);
    // 图片大小为窗口高度的一半
    int imgSize = MTWheight / 2;

    // 获取DLL所在目录
    HMODULE hModule = g_hInstance;
    wchar_t dllPath[MAX_PATH];
    GetModuleFileName(hModule, dllPath, MAX_PATH);

    // 提取目录路径
    wchar_t* lastSlash = wcsrchr(dllPath, L'\\');
    if (lastSlash) {
        *(lastSlash + 1) = L'\0';
    }

    // 构建图片目录路径
    wchar_t picturePath[MAX_PATH];
    wcscpy_s(picturePath, dllPath);
    wcscat_s(picturePath, L"pictures\\");

    // 绘制14张图片，两行每行7个
    for (int i = 0; i < 14; i++) {
        // 先取出本次要获取的图片id
        DWORD currentMagicValue = *magicValues[i];

        // 使用对应的全局变量值作为图片名
        wchar_t fileName[MAX_PATH];
        swprintf_s(fileName, L"%s%d.png", picturePath, currentMagicValue);

        // 加载图片
        Bitmap* img = new Bitmap(fileName);
        if (img && img->GetLastStatus() == Ok) {
            // 计算绘制位置
            int row = i / 7;        // 行号 (0或1)
            int col = i % 7;        // 列号 (0-6)
            int x = col * imgSize;  // x坐标
            int y = row * imgSize;  // y坐标

            // 绘制图片到内存画布
            g_MTWpngGph->DrawImage(img, x, y, imgSize, imgSize);
        }

        // 释放图片资源
        if (img) {
            delete img;
        }
    }
}


void MTWdrawBmp() {
    if (!g_MTWbgBmp) return;
    // 先清空目标位图（可选）
    g_MTWshowGph->Clear(Color::Transparent);
    
    // 第一步：将背景位图绘制到显示位图
    g_MTWshowGph->DrawImage(g_MTWbgBmp, 0, 0);
    //第二步，画高亮区域
    if (nowMagic > 0 && nowMagic <= 14) {
        // 计算区域位置
        int cellWidth = MTWwidth / 7;
        int cellHeight = MTWheight / 2;
        int borderWidth = 3;  // 边框宽度，避免涂到边框

        // 计算行列
        int row = (nowMagic - 1) / 7;  // 行号 (0或1)
        int col = (nowMagic - 1) % 7;  // 列号 (0-6)

        // 计算绘制区域，留出边框空间
        int x = col * cellWidth + borderWidth;
        int y = row * cellHeight + borderWidth;
        int width = cellWidth - borderWidth * 2;
        int height = cellHeight - borderWidth * 2;

        // 创建半透明白色画刷
        SolidBrush whiteBrush(Color(255, 255, 255, 255)); 
        //绘制高亮区域
        g_MTWshowGph->FillRectangle(&whiteBrush, x, y, width, height);
    }
     //第三步：将PNG位图叠加到显示位图上
    g_MTWshowGph->DrawImage(g_MTWpngBmp, 0, 0);
}


//*************************************************创建子窗口*****************************************************
//窗口过程函数
LRESULT CALLBACK MagicTableWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {

    //消息识别
    switch (message) {

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (!g_MTWscreenGph) {
            g_MTWscreenGph = new Graphics(hdc);
        }
        else {
            g_MTWscreenGph->ReleaseHDC(hdc);  // 释放旧的
            g_MTWscreenGph = Graphics::FromHDC(hdc);  // 绑定新的
        }

        g_MTWscreenGph->DrawImage(g_MTWshowBmp, 0, 0);
        EndPaint(hWnd, &ps);
    }
    break;
    case WM_MOUSEMOVE:
    {
        // 获取鼠标在窗口中的坐标
        int mouseX = LOWORD(lParam);
        int mouseY = HIWORD(lParam);
        uint8_t newMagic = 0;   //使用字节类型
        // 检查鼠标是否在窗口范围内
        if (mouseX >= 0 && mouseX < MTWwidth && mouseY >= 0 && mouseY < MTWheight) {
            // 计算每个区域的大小
            int cellWidth = MTWwidth / 7;   // 每列宽度
            int cellHeight = MTWheight / 2; // 每行高度
            // 计算鼠标所在的列和行
            int col = mouseX / cellWidth;   // 列号 (0-6)
            int row = mouseY / cellHeight;  // 行号 (0-1)
            // 确保不越界
            if (col >= 0 && col < 7 && row >= 0 && row < 2) {
                // 计算区域序号：第一行1-7，第二行8-14
                newMagic = row * 7 + col + 1;
                if (newMagic != nowMagic) {
                    nowMagic = newMagic;
                    WriteMagicSlot();  // 在选择魔法时写入内存
                    MTWdrawBmp();
                    InvalidateRect(hWnd, NULL, FALSE);  // 触发重绘
                }
            }
        }
    }
    break;
    case WM_LBUTTONDOWN: {          //游戏原本就有“单击鼠标左键关闭esc菜单窗口”，这里同步一下也关闭MTW窗口
        ShowWindow(MagicTableWindow, SW_HIDE);
        disableTab = true;
    }
    break;
    /*鼠标滚轮版
    case WM_MOUSEWHEEL:
    {
        int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        int steps = wheelDelta / 120;
        int newMagic = nowMagic;
        newMagic += steps;
        // 循环处理：超出范围时回到另一端
        while (newMagic > 14) {
            newMagic -= 14;
        }
        while (newMagic < 1) {
            newMagic += 14;
        }
        if (newMagic != nowMagic) {
            nowMagic = newMagic;
            MTWdrawBmp();
            InvalidateRect(hWnd, NULL, FALSE);  // 触发重绘
        }
    }
    break;
     */                  


    //默认消息，用于处理没写的情况
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);

    }
}
//注册窗口类函数，MagicTableWindow缩写为MTW
BOOL RegistMTWClass() {                 //BOOL和int类似吧，用来做函数返回值，两个核心值：非零为TRUE，零为FALSE；void就没有返回值
    WNDCLASSEXW wc = {};               //wc自定义结构体变量，意为WindowClass
    wc.cbSize = sizeof(WNDCLASSEXW);   //定义大小（以字节为单位）
    wc.lpfnWndProc = MagicTableWindowProc;   //绑定窗口过程函数
    wc.hInstance = g_hInstance;             //dll句柄
    wc.lpszClassName = L"MTWclass";     //自定义窗口类名
    wc.hbrBackground = (HBRUSH)COLOR_WINDOW;    //= (HBRUSH)COLOR_WINDOW窗口背景白色，不写就没有背景色,但没有背景色会把第一次显示时的游戏画面当背景,空画刷(HBRUSH)GetStockObject(NULL_BRUSH)也是一样
    wc.hCursor = LoadCursor(NULL, IDC_CROSS);    // LoadCursorW(NULL, IDC_ARROW)-默认鼠标箭头。现在用的是十字，默认箭头有点出戏，自建的目前还不会


    ATOM atom = RegisterClassExW(&wc);
    //处理注册错误，
    if (atom == 0) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            MessageBoxW(NULL, L"窗口类注册失败！", L"错误", MB_ICONERROR);        //MB_ICONERROR窗口风格-红色错误图标
            return FALSE;
        }
    }
    return TRUE;            //因为用了BOOL，所以要返回
}
//创建MTW窗口
BOOL CreateMTW() {  

    // 检查游戏窗口句柄
    if (!g_hGameWindow) {
        MessageBoxW(NULL, L"错误：游戏窗口句柄为空！", L"错误", MB_ICONERROR);
        return FALSE;
    }

    if (!IsWindow(g_hGameWindow)) {
        MessageBoxW(NULL, L"错误：游戏窗口句柄无效！", L"错误", MB_ICONERROR);
        return FALSE;
    }
    // 获取游戏窗口信息
    RECT gameRect;
    GetClientRect(g_hGameWindow, &gameRect);            //GetClientRect是客户区，GetWindowRect是整个窗口（含标题栏/边框）
    gameWidth = gameRect.right - gameRect.left;
    gameHeight = gameRect.bottom - gameRect.top;
    MTWwidth = static_cast<int>(gameHeight * g_config.windowSize *0.07);  // 计算覆盖窗口的位置和大小
    MTWheight = static_cast<int>(gameHeight * g_config.windowSize *0.02);    //宽使用0.7h，高使用0.2h---高改为0.24h,宽为0.84
    MTWx = gameRect.left + (gameWidth - MTWwidth) / 2;
    MTWy = gameRect.top + (gameHeight * g_config.windowPosition *0.01);
    //注册窗口类
    RegistMTWClass();
    //创建窗口（调用CreateWindowEx函数会返回窗口句柄，存入MagicTableWindow）
    MagicTableWindow = CreateWindowExW(                 //CreateWindowExW需要12个参数
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE,              //1.扩展窗口样式,用"|"定义多个参数，系统会整合成一个；WS_EX_LAYERED支持透明度，WS_EX_TRANSPARENT鼠标点击穿透
        L"MTWclass",                                    //2.窗口类名
        L"魔法轮盘标题",                                //3.标题，好像需要WS_CAPTION才会显示
        WS_POPUP,                          //4.基础窗口样式,WS_VISIBLE窗口可见,WS_CHILD子窗口
        MTWx, MTWy, MTWwidth, MTWheight,                      //5-8.窗口坐标x, y（左上角点）、窗口大小；均需自己定义；x, y简化为直接写0，0，为父窗口的左上角点，长宽只支持像素不支持百分比，还需先获取游戏的
        nullptr,                                  //9.父窗口句柄
        nullptr,                                        //10.菜单句柄
        g_hInstance,                                    //11.模块句柄
        nullptr                                        //12.传给窗口过程函数的lpParam，应该用不到;;另外，最后一个参数句尾不要写“逗号”
    );
    if (MagicTableWindow) {
        SetParent(MagicTableWindow, g_hGameWindow);  // 创建后再设置父子关系，在创建时设置子窗口会有点bug
    }
    if (!MagicTableWindow) {
        DWORD error = GetLastError();
        WCHAR errorMsg[521];
        FormatMessageW(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            error,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            errorMsg,
            sizeof(errorMsg) / sizeof(WCHAR),
            NULL
        );
        WCHAR fullMsg[1024];
        StringCchPrintfW(fullMsg, _countof(fullMsg), L"窗口创建失败！\n错误码：%lu\n错误描述：%s", error, errorMsg);
        MessageBoxW(NULL, fullMsg, L"错误", MB_ICONERROR);
        return FALSE;
    }
    // 设置整个窗口透明度
    SetLayeredWindowAttributes(MagicTableWindow, 0, MTW_ALPHA, LWA_ALPHA);      //0是颜色键透明模式用的，因为这里用的是LWA_ALPHA模式，所以填0就行
    return TRUE;
}

//*******************************************线程***************************************************
//检测游戏窗口大小是否改变、记忆是否坐火更换
void MTWCheckingThread() {
    Sleep(15000);
    // 保存所有14个变量的旧值
    DWORD* oldValues[] = { &oldg_M1, &oldg_M2, &oldg_M3, &oldg_M4, &oldg_M5, &oldg_M6, &oldg_M7,
                          &oldg_M8, &oldg_M9, &oldg_M10, &oldg_M11, &oldg_M12, &oldg_M13, &oldg_M14 };
    DWORD* currentValues[] = { &g_M1, &g_M2, &g_M3, &g_M4, &g_M5, &g_M6, &g_M7,
                              &g_M8, &g_M9, &g_M10, &g_M11, &g_M12, &g_M13, &g_M14 };
    while (!g_shouldExit) {

        ReadGameMemory();
        // 检查是否有任何一个值发生变化
        bool hasChanged = false;
        for (int i = 0; i < 14; i++) {
            if (*oldValues[i] != *currentValues[i]) {
                hasChanged = true;
                break;
            }
        }
        if (hasChanged) {
            // 保存当前值到旧值
            for (int i = 0; i < 14; i++) {
                *oldValues[i] = *currentValues[i];
            }
            MTWdrawImgs();
            MTWdrawBmp();
        }
        RECT gameRect;
        GetClientRect(g_hGameWindow, &gameRect);            //GetClientRect是客户区，GetWindowRect是整个窗口（含标题栏/边框）
        if (gameWidth != (gameRect.right - gameRect.left)) {
            gameWidth = gameRect.right - gameRect.left;
            gameHeight = gameRect.bottom - gameRect.top;
            MTWwidth = static_cast<int>(gameHeight * g_config.windowSize * 0.07);  // 计算覆盖窗口的位置和大小
            MTWheight = static_cast<int>(gameHeight * g_config.windowSize * 0.02);    //宽使用0.7h，高使用0.2h---高改为0.24h,宽为0.84
            MTWx = gameRect.left + (gameWidth - MTWwidth) / 2;
            MTWy = gameRect.top + (gameHeight * g_config.windowPosition * 0.01);
            SetWindowPos(MagicTableWindow, NULL, MTWx, MTWy, MTWwidth, MTWheight,
                SWP_NOZORDER);
            MTWdestroyBmps();
            MTWcreateBmps();
            MTWdrawbg();
            MTWdrawImgs();
            MTWdrawBmp();
        }
        Sleep(1000);
    }
}
//检测小键盘数字键
void MTWNumpadItemsThread() {
    Sleep(15000);
    while (!g_shouldExit) {
        if (GetForegroundWindow() == g_hGameWindow) {
            // 检测小键盘数字键切换物品 - 只有启用了NumpadItems功能才响应
            if (g_config.enableNumpadItems) {
                if (GetAsyncKeyState(VK_NUMPAD1) & 0x0001) {
                    WriteEquipItemAddress(0);
                }
                if (GetAsyncKeyState(VK_NUMPAD2) & 0x0001) {
                    WriteEquipItemAddress(1);
                }
                if (GetAsyncKeyState(VK_NUMPAD3) & 0x0001) {
                    WriteEquipItemAddress(2);
                }
                if (GetAsyncKeyState(VK_NUMPAD4) & 0x0001) {
                    WriteEquipItemAddress(3);
                }
                if (GetAsyncKeyState(VK_NUMPAD5) & 0x0001) {
                    WriteEquipItemAddress(4);
                }
                if (GetAsyncKeyState(VK_NUMPAD6) & 0x0001) {
                    WriteEquipItemAddress(5);
                }
                if (GetAsyncKeyState(VK_NUMPAD7) & 0x0001) {
                    WriteEquipItemAddress(6);
                }
                if (GetAsyncKeyState(VK_NUMPAD8) & 0x0001) {
                    WriteEquipItemAddress(7);
                }
                if (GetAsyncKeyState(VK_NUMPAD9) & 0x0001) {
                    WriteEquipItemAddress(8);
                }
                if (GetAsyncKeyState(VK_NUMPAD0) & 0x0001) {
                    WriteEquipItemAddress(9);
                }
            }
        }
        Sleep(5);
    }
}
//TAB打开窗口和其他一些功能
void MTWKeyCheckThread() {
    Sleep(15000);
    while (!g_shouldExit) {
        if (GetForegroundWindow() == g_hGameWindow) {
            // 使用配置文件中的按键设置


            if (g_config.enableRollSprint && GetAsyncKeyState(g_config.modRollKey) & 0x0001) {
                // 翻滚
                SimulateRealKeyPress(g_config.rollKey);
            }

            if (g_config.enableRollSprint && GetAsyncKeyState(g_config.sprintToggleKey) & 0x0001) {
                // 疾跑切换键
                ToggleSprintMode();
            }
            
            //QuickSlots
            if (g_config.enableQuickSlots) {
                if (GetAsyncKeyState(g_config.quickSlot1Key) & 0x0001) {
                    QuickSlotsKeyPress(g_config.switchMagicKey);
                }
                if (GetAsyncKeyState(g_config.quickSlot2Key) & 0x0001) {
                    QuickSlotsKeyPress(g_config.switchItemKey);
                }
                if (GetAsyncKeyState(g_config.quickSlot4Key) & 0x0001) {
                    QuickSlotsKeyPress(g_config.switchRightWeaponKey);
                }
                if (GetAsyncKeyState(g_config.quickSlot3Key) & 0x0001) {
                    QuickSlotsKeyPress(g_config.switchLeftWeaponKey);
                }
            }
            // MTW
            if (g_config.enableMagicTable) {
                bool isModRollPressed = (GetAsyncKeyState(g_config.magicTableKey) & 0x8000) != 0;       //用于判断按住、松开相应按键
                if (isTABPressdLastTime == false && isModRollPressed == true) {                 //按下
                    ShowWindow(MagicTableWindow, SW_SHOW);                              //显示窗口
                    SimulateRealKeyPress(VK_ESCAPE);                                //模拟按一次esc键打开游戏的esc菜单（用于解锁鼠标）
                    disableTab = false;
                }
                if (isTABPressdLastTime == true && isModRollPressed == false && disableTab == false) {
                    ShowWindow(MagicTableWindow, SW_HIDE);
                    SimulateRealKeyPress(VK_ESCAPE);
                }
                isTABPressdLastTime = isModRollPressed;
            }
        }

        Sleep(5);
    }
}
//初始化
DWORD WINAPI ModInitThread(LPVOID lpParam) {

    Sleep(9000);        //停9秒
    g_hGameWindow = FindGameWindow(); //获取游戏窗口句柄
    CreateMTW();
    InstallLowLevelHook();          //键盘钩子，没有用但留着不改了
    MTWcreateBmps();                //创建内存画布
    MTWdrawbg();                    //画背景
    MTWdrawImgs();                  //画图片
    MTWdrawBmp();                   //最终显示用的，把背景画到这里，再画鼠标区域高亮，最后画图片

    Sleep(2000);

    if (!g_hGameWindow) {
        return 1;
    }

    if (!CreateMTW()) {
        return 1;
    }

    // 添加消息循环，让线程保持活跃
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    UninstallLowLevelHook();//键盘钩子
    return 0;
}
//**********************************************************************************************************************
BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(hModule);     //禁用线程通知，不加这一行游戏直接隐藏挂到后台，可能是游戏本身有大量线程，而这里没有处理这些线程创建通知，导致错误
        g_hInstance = hModule;  //  这里获取并保存DLL句柄
        GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);      //初始化GDI+ （2/2）

        LoadConfig();

        CreateThread(NULL, 0, ModInitThread, NULL, 0, NULL);
        std::thread(MTWCheckingThread).detach();        //循查窗口大小和内存

        std::thread MTWt(MTWKeyCheckThread);
        std::thread(MTWNumpadItemsThread).detach();
        MTWt.detach();

        //CreateMTW();
        break;
    }
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        g_shouldExit = true;  // 通知线程退出
        Sleep(100);           // 给线程一点时间退出  
        GdiplusShutdown(gdiplusToken);

        break;
    }
    return TRUE;
}