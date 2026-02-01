#include <opencv2/opencv.hpp>
#include <iostream>
#include <filesystem>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>

#pragma comment(lib, "comctl32.lib")

namespace fs = std::filesystem;

// ============ GUI Control IDs ============
#define ID_BTN_BROWSE_VIDEO 101
#define ID_BTN_BROWSE_OUTPUT 102
#define ID_BTN_START 103
#define ID_EDT_VIDEO 104
#define ID_EDT_OUTPUT 105
#define ID_EDT_FRAMESTEP 106
#define ID_LBL_INFO 107
#define ID_PROGRESS 108

// ============ Global Variables ============
HWND hWndMain;
HWND hEdtVideo, hEdtOutput, hEdtFrameStep;
HWND hLblInfo, hProgress;
HWND hBtnStart;

std::string g_videoPath;
std::string g_outputDir;
int g_totalFrames = 0;
double g_fps = 0;

std::atomic<bool> g_isProcessing(false);

// ============ Frame Task Queue ============
struct FrameTask
{
    cv::Mat frame;
    int index;
};

std::queue<FrameTask> taskQueue;
std::mutex queueMutex;
std::condition_variable cvQueue;
std::atomic<bool> finished(false);

// ============ Writer Thread ============
void writerThread(const std::string& outputDir)
{
    while (true)
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        cvQueue.wait(lock, [] {
            return !taskQueue.empty() || finished.load();
        });

        if (taskQueue.empty() && finished)
            break;

        FrameTask task = taskQueue.front();
        taskQueue.pop();
        lock.unlock();

        std::string filename =
            outputDir + "\\frame_" + std::to_string(task.index) + ".jpg";

        cv::imwrite(filename, task.frame,
                    {cv::IMWRITE_JPEG_QUALITY, 95});
    }
}

// ============ Update Video Info ============
void UpdateVideoInfo(const std::string& videoPath)
{
    cv::VideoCapture cap(videoPath);
    if (!cap.isOpened())
    {
        SetWindowTextW(hLblInfo, L"❌ Không thể mở video!");
        g_totalFrames = 0;
        g_fps = 0;
        return;
    }

    g_fps = cap.get(cv::CAP_PROP_FPS);
    g_totalFrames = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
    
    int width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    int height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    double duration = g_totalFrames / g_fps;

    wchar_t info[512];
    swprintf(info, 512,
        L"📊 Video Info:\r\n"
        L"   🎞 Tổng số frame: %d\r\n"
        L"   🎥 FPS: %.2f\r\n"
        L"   📐 Kích thước: %dx%d\r\n"
        L"   ⏱ Thời lượng: %.1f giây",
        g_totalFrames, g_fps, width, height, duration);
    
    SetWindowTextW(hLblInfo, info);
    cap.release();
}

// ============ Process Video ============
void ProcessVideo(const std::string& videoPath, const std::string& outputDir, int frameStep)
{
    g_isProcessing = true;
    EnableWindow(hBtnStart, FALSE);

    finished = false;
    while (!taskQueue.empty()) taskQueue.pop();

    fs::create_directories(outputDir);

    cv::VideoCapture cap(videoPath);
    if (!cap.isOpened())
    {
        MessageBoxW(hWndMain, L"Không thể mở video!", L"Lỗi", MB_ICONERROR);
        g_isProcessing = false;
        EnableWindow(hBtnStart, TRUE);
        return;
    }

    int totalFrames = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
    
    // Setup progress bar
    SendMessage(hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, totalFrames));
    SendMessage(hProgress, PBM_SETPOS, 0, 0);

    // Start writer threads
    unsigned int threadCount = std::thread::hardware_concurrency();
    std::vector<std::thread> writers;
    for (unsigned int i = 0; i < threadCount; ++i)
        writers.emplace_back(writerThread, outputDir);

    cv::Mat frame;
    int frameIndex = 0;
    int savedIndex = 0;

    while (cap.read(frame))
    {
        if (frameIndex % frameStep == 0)
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            taskQueue.push({frame.clone(), savedIndex++});
            cvQueue.notify_one();
        }
        frameIndex++;

        // Update progress
        SendMessage(hProgress, PBM_SETPOS, frameIndex, 0);
    }

    finished = true;
    cvQueue.notify_all();

    for (auto& t : writers)
        t.join();

    cap.release();

    wchar_t msg[256];
    swprintf(msg, 256, L"✅ Hoàn thành!\nĐã trích xuất %d ảnh.", savedIndex);
    MessageBoxW(hWndMain, msg, L"Thành công", MB_ICONINFORMATION);

    g_isProcessing = false;
    EnableWindow(hBtnStart, TRUE);
}

// ============ Browse Video File ============
void BrowseVideoFile()
{
    wchar_t filename[MAX_PATH] = L"";
    
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWndMain;
    ofn.lpstrFilter = L"Video Files\0*.mp4;*.avi;*.mkv;*.mov;*.wmv\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn))
    {
        SetWindowTextW(hEdtVideo, filename);
        
        // Convert to string
        int len = WideCharToMultiByte(CP_UTF8, 0, filename, -1, NULL, 0, NULL, NULL);
        g_videoPath.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, filename, -1, &g_videoPath[0], len, NULL, NULL);
        g_videoPath.pop_back(); // Remove null terminator

        UpdateVideoInfo(g_videoPath);
    }
}

// ============ Browse Output Folder ============
void BrowseOutputFolder()
{
    wchar_t path[MAX_PATH];
    
    BROWSEINFOW bi = {0};
    bi.hwndOwner = hWndMain;
    bi.lpszTitle = L"Chọn thư mục lưu ảnh";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl)
    {
        SHGetPathFromIDListW(pidl, path);
        SetWindowTextW(hEdtOutput, path);
        
        int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
        g_outputDir.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, path, -1, &g_outputDir[0], len, NULL, NULL);
        g_outputDir.pop_back();

        CoTaskMemFree(pidl);
    }
}

// ============ Start Extraction ============
void StartExtraction()
{
    if (g_isProcessing)
    {
        MessageBoxW(hWndMain, L"Đang xử lý...", L"Thông báo", MB_ICONWARNING);
        return;
    }

    if (g_videoPath.empty())
    {
        MessageBoxW(hWndMain, L"Vui lòng chọn video!", L"Thông báo", MB_ICONWARNING);
        return;
    }

    if (g_outputDir.empty())
    {
        MessageBoxW(hWndMain, L"Vui lòng chọn thư mục output!", L"Thông báo", MB_ICONWARNING);
        return;
    }

    wchar_t buffer[32];
    GetWindowTextW(hEdtFrameStep, buffer, 32);
    int frameStep = _wtoi(buffer);

    if (frameStep <= 0)
    {
        MessageBoxW(hWndMain, L"Số frame phải lớn hơn 0!", L"Thông báo", MB_ICONWARNING);
        return;
    }

    // Run in separate thread
    std::thread(ProcessVideo, g_videoPath, g_outputDir, frameStep).detach();
}

// ============ Window Procedure ============
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_BTN_BROWSE_VIDEO:
            BrowseVideoFile();
            break;
        case ID_BTN_BROWSE_OUTPUT:
            BrowseOutputFolder();
            break;
        case ID_BTN_START:
            StartExtraction();
            break;
        }
        break;

    case WM_CLOSE:
        if (g_isProcessing)
        {
            if (MessageBoxW(hwnd, L"Đang xử lý. Bạn có muốn thoát?", 
                L"Xác nhận", MB_YESNO | MB_ICONQUESTION) == IDNO)
                return 0;
        }
        DestroyWindow(hwnd);
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// ============ Create GUI ============
void CreateGUI(HWND hwnd)
{
    HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    int y = 20;

    // === Video Section ===
    CreateWindowW(L"STATIC", L"📁 File Video:", WS_VISIBLE | WS_CHILD,
        20, y, 100, 25, hwnd, NULL, NULL, NULL);
    
    hEdtVideo = CreateWindowW(L"EDIT", L"", 
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_READONLY | ES_AUTOHSCROLL,
        130, y, 380, 25, hwnd, (HMENU)ID_EDT_VIDEO, NULL, NULL);
    
    HWND hBtnBrowseVideo = CreateWindowW(L"BUTTON", L"Chọn...", 
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        520, y, 70, 25, hwnd, (HMENU)ID_BTN_BROWSE_VIDEO, NULL, NULL);
    
    y += 40;

    // === Video Info ===
    hLblInfo = CreateWindowW(L"STATIC", L"Chưa chọn video", 
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        20, y, 570, 100, hwnd, (HMENU)ID_LBL_INFO, NULL, NULL);
    
    y += 110;

    // === Frame Step ===
    CreateWindowW(L"STATIC", L"🔢 Số frame lấy 1 ảnh:", WS_VISIBLE | WS_CHILD,
        20, y, 160, 25, hwnd, NULL, NULL, NULL);
    
    hEdtFrameStep = CreateWindowW(L"EDIT", L"30", 
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER | ES_CENTER,
        185, y, 80, 25, hwnd, (HMENU)ID_EDT_FRAMESTEP, NULL, NULL);
    
    CreateWindowW(L"STATIC", L"(VD: nhập 30 = mỗi 30 frame lấy 1 ảnh)", 
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        280, y, 300, 25, hwnd, NULL, NULL, NULL);
    
    y += 40;

    // === Output Folder ===
    CreateWindowW(L"STATIC", L"📂 Thư mục output:", WS_VISIBLE | WS_CHILD,
        20, y, 120, 25, hwnd, NULL, NULL, NULL);
    
    hEdtOutput = CreateWindowW(L"EDIT", L"", 
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_READONLY | ES_AUTOHSCROLL,
        150, y, 360, 25, hwnd, (HMENU)ID_EDT_OUTPUT, NULL, NULL);
    
    HWND hBtnBrowseOutput = CreateWindowW(L"BUTTON", L"Chọn...", 
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        520, y, 70, 25, hwnd, (HMENU)ID_BTN_BROWSE_OUTPUT, NULL, NULL);
    
    y += 45;

    // === Progress Bar ===
    hProgress = CreateWindowW(PROGRESS_CLASSW, L"", 
        WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
        20, y, 570, 25, hwnd, (HMENU)ID_PROGRESS, NULL, NULL);
    
    y += 40;

    // === Start Button ===
    hBtnStart = CreateWindowW(L"BUTTON", L"▶ BẮT ĐẦU TRÍCH XUẤT", 
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        180, y, 250, 40, hwnd, (HMENU)ID_BTN_START, NULL, NULL);

    // Apply font to all controls
    EnumChildWindows(hwnd, [](HWND child, LPARAM lParam) -> BOOL {
        SendMessage(child, WM_SETFONT, lParam, TRUE);
        return TRUE;
    }, (LPARAM)hFont);
}

// ============ Main Entry Point ============
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    CoInitialize(NULL);
    InitCommonControls();

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"FrameExtractorClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    RegisterClassW(&wc);

    hWndMain = CreateWindowW(
        L"FrameExtractorClass",
        L"🎬 Video Frame Extractor - SnapcutVid",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        630, 420,
        NULL, NULL, hInstance, NULL
    );

    CreateGUI(hWndMain);

    ShowWindow(hWndMain, nCmdShow);
    UpdateWindow(hWndMain);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}
