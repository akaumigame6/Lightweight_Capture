#include <windows.h>
#include <fstream>
#include <string>
#include <iostream>
#include <commdlg.h> // ファイル保存ダイアログ用
#include <gdiplus.h> // PNG保存用 (GDI+)
#include <ctime>     // 日時取得用
#include <sstream>
#include <iomanip>

// --- 設定・データ ---
#include <vector>
#include <shobjidl.h> // IFileOpenDialog 用

struct Config { int x = 100, y = 100, w = 400, h = 300; };
Config cfg;
struct SavedConfig { std::string name; int x, y, w, h; };
std::vector<SavedConfig> configList;

const std::string CONFIG_FILE = "config.txt";
const std::string SAVEDIR_FILE = "savedir.txt";
std::string saveDir = ""; // 保存先のフォルダ
HWND g_hwnd = NULL; // 親ウィンドウのハンドルを保存する変数

void LoadSaveDir() {
    std::ifstream f(SAVEDIR_FILE);
    if (f.is_open()) {
        std::getline(f, saveDir);
    }
}

void SaveSaveDir() {
    std::ofstream f(SAVEDIR_FILE);
    f << saveDir;
}

void LoadAllConfigs() {
    configList.clear();
    std::ifstream f(CONFIG_FILE);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string name; int x, y, w, h;
        iss >> name >> x >> y >> w >> h;
        if (!iss.fail()) {
            configList.push_back({name, x, y, w, h});
        } else {
            std::istringstream iss_old(line);
            iss_old >> x >> y >> w >> h;
            if (!iss_old.fail()) configList.push_back({"前回終了時の位置", x, y, w, h});
        }
    }
    if (configList.empty() || configList[0].name != "前回終了時の位置") {
        configList.insert(configList.begin(), {"前回終了時の位置", 100, 100, 400, 300});
    }
    cfg.x = configList[0].x; cfg.y = configList[0].y;
    cfg.w = configList[0].w; cfg.h = configList[0].h;
}

void SaveAllConfigs() {
    if (configList.empty()) {
        configList.push_back({"前回終了時の位置", cfg.x, cfg.y, cfg.w, cfg.h});
    } else {
        configList[0].x = cfg.x; configList[0].y = cfg.y;
        configList[0].w = cfg.w; configList[0].h = cfg.h;
    }
    std::ofstream f(CONFIG_FILE);
    for (const auto& c : configList) {
        f << c.name << " " << c.x << " " << c.y << " " << c.w << " " << c.h << "\n";
    }
}

// --- ファイル名に日時を追加する関数 ---
std::string GetDateTimeFilename() {
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S"); // 年月日_時分秒
    
    std::string timeStr = oss.str();
    std::string fileName = "capture_" + timeStr + ".png";
    
    if (saveDir.empty()) {
        // 空白時は、exeと同じ階層に「pictures」フォルダを作ってそこに保存する
        CreateDirectoryA("pictures", NULL); // 既に存在する場合は何もしない
        return "pictures\\" + fileName;
    } else {
        if (saveDir.back() != '\\' && saveDir.back() != '/') {
            return saveDir + "\\" + fileName;
        }
        return saveDir + fileName;
    }
}

// --- GDI+用エンコーダ取得関数 ---
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    Gdiplus::ImageCodecInfo* pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    free(pImageCodecInfo);
    return -1;
}

// --- PNG保存関数 ---
bool SaveImagePNG(HBITMAP hBitmap, const std::string& path) {
    Gdiplus::Bitmap bitmap(hBitmap, NULL);
    CLSID clsid;
    if (GetEncoderClsid(L"image/png", &clsid) != -1) {
        // C++の文字形式からWindows標準のワイド文字に変換して保存
        int len = MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, NULL, 0);
        std::wstring wpath(len, 0);
        MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, &wpath[0], len);
        return bitmap.Save(wpath.c_str(), &clsid, NULL) == Gdiplus::Ok;
    }
    return false;
}

// --- キャプチャ処理 ---
void CaptureScreen(HWND hwnd) {
    RECT rc;
    int cx, cy, cw, ch;

    // 最大化されているか判定
    if (IsZoomed(hwnd)) {
        // 最大化時：作業領域（タスクバーを除く）を取得
        RECT workArea;
        SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
        cx = workArea.left;
        cy = workArea.top;
        cw = workArea.right - workArea.left;
        ch = workArea.bottom - workArea.top;
    } else {
        // 通常時：クライアント領域（透明部分のみ）
        POINT pt = {0, 0};
        ClientToScreen(hwnd, &pt);
        GetClientRect(hwnd, &rc);
        cx = pt.x; cy = pt.y;
        cw = rc.right; ch = rc.bottom;
        
        // 通常時のみ設定を上書き保存
        cfg.x = cx; cfg.y = cy; cfg.w = cw; cfg.h = ch;
        SaveAllConfigs();
    }

    // ウィンドウが移りこまないように、フォーカスを保ったまま透明度を0に
    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_ALPHA);
    Sleep(100); // 移動が反映されるまで少し待つ

    // 画面キャプチャ
    HDC hScreenDC = GetDC(NULL);
    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, cw, ch);
    SelectObject(hMemoryDC, hBitmap);
    BitBlt(hMemoryDC, 0, 0, cw, ch, hScreenDC, cx, cy, SRCCOPY);

    //日時付きの新しいファイル名を作成して保存する。
    std::string finalPath = GetDateTimeFilename();
    SaveImagePNG(hBitmap, finalPath);

    // 後始末と再表示
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hScreenDC);
    
    // ウィンドウを元の透明度に戻す
    SetLayeredWindowAttributes(hwnd, RGB(255, 0, 255), 255, LWA_COLORKEY | LWA_ALPHA);
    MessageBeep(MB_OK); // 完了音
}

#define ID_COMBO 101
#define ID_BTN_APPLY 102
#define ID_BTN_DELETE 103
#define ID_EDIT_NAME 104
#define ID_EDIT_X 105
#define ID_EDIT_Y 106
#define ID_EDIT_W 107
#define ID_EDIT_H 108
#define ID_BTN_SAVE_INPUT 109
#define ID_BTN_SAVE_CURRENT 110

 // --- シンプルな座標指定用サブウィンドウプロシージャ ---
LRESULT CALLBACK InputSubWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hEdit;
    switch (msg) {
        case WM_CREATE: {
            char buf[100];
            sprintf_s(buf, "%d %d %d %d", cfg.x, cfg.y, cfg.w, cfg.h);
            hEdit = CreateWindowExA(0, "EDIT", buf, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                10, 10, 200, 25, hwnd, NULL, NULL, NULL);
            CreateWindowExA(0, "BUTTON", "適用", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                220, 10, 50, 25, hwnd, (HMENU)1, NULL, NULL);
            break;
        }
        case WM_COMMAND: {
            if (LOWORD(wp) == 1) {
                char buf[100]; GetWindowTextA(hEdit, buf, sizeof(buf));
                int nx, ny, nw, nh;
                if (sscanf_s(buf, "%d %d %d %d", &nx, &ny, &nw, &nh) == 4) {
                    if (nw < 50 || nh < 50) { MessageBoxA(hwnd, "幅と高さは50以上にしてください。", "エラー", MB_OK); break; }
                    cfg.x = nx; cfg.y = ny; cfg.w = nw; cfg.h = nh;
                    RECT winRect = {0, 0, cfg.w, cfg.h};
                    AdjustWindowRect(&winRect, WS_OVERLAPPEDWINDOW, FALSE);
                    SetWindowPos(g_hwnd, NULL, cfg.x + winRect.left, cfg.y + winRect.top, 
                                winRect.right - winRect.left, winRect.bottom - winRect.top, SWP_NOZORDER);
                    SaveAllConfigs();
                    DestroyWindow(hwnd);
                } else {
                    MessageBoxA(hwnd, "フォーマットが間違っています。\n例: 100 100 800 600", "エラー", MB_OK);
                }
            }
            break;
        }
        case WM_DESTROY: {
            EnableWindow(g_hwnd, TRUE);
            SetForegroundWindow(g_hwnd);
            break;
        }
        default: return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

 // --- サブウィンドウ（入力用）プロシージャ ---
LRESULT CALLBACK SubWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hCombo, hEditName, hEditX, hEditY, hEditW, hEditH;
    switch (msg) {
        case WM_CREATE: {
            CreateWindowExA(0, "STATIC", "登録済み設定:", WS_CHILD | WS_VISIBLE, 10, 10, 130, 25, hwnd, NULL, NULL, NULL);
            hCombo = CreateWindowExA(0, "COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 140, 10, 200, 150, hwnd, (HMENU)ID_COMBO, NULL, NULL);
            CreateWindowExA(0, "BUTTON", "適用", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 350, 10, 60, 25, hwnd, (HMENU)ID_BTN_APPLY, NULL, NULL);
            CreateWindowExA(0, "BUTTON", "削除", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 420, 10, 60, 25, hwnd, (HMENU)ID_BTN_DELETE, NULL, NULL);

            CreateWindowExA(0, "STATIC", "新規/更新:", WS_CHILD | WS_VISIBLE, 10, 50, 100, 25, hwnd, NULL, NULL, NULL);
            CreateWindowExA(0, "STATIC", "名前:", WS_CHILD | WS_VISIBLE, 10, 80, 60, 25, hwnd, NULL, NULL, NULL);
            hEditName = CreateWindowExA(0, "EDIT", "新しい設定", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 80, 80, 100, 25, hwnd, (HMENU)ID_EDIT_NAME, NULL, NULL);

            CreateWindowExA(0, "STATIC", "X:", WS_CHILD | WS_VISIBLE, 190, 80, 20, 20, hwnd, NULL, NULL, NULL);
            hEditX = CreateWindowExA(0, "EDIT", "100", WS_CHILD | WS_VISIBLE | WS_BORDER, 210, 80, 40, 25, hwnd, (HMENU)ID_EDIT_X, NULL, NULL);
            CreateWindowExA(0, "STATIC", "Y:", WS_CHILD | WS_VISIBLE, 260, 80, 20, 20, hwnd, NULL, NULL, NULL);
            hEditY = CreateWindowExA(0, "EDIT", "100", WS_CHILD | WS_VISIBLE | WS_BORDER, 280, 80, 40, 25, hwnd, (HMENU)ID_EDIT_Y, NULL, NULL);
            CreateWindowExA(0, "STATIC", "W:", WS_CHILD | WS_VISIBLE, 330, 80, 20, 20, hwnd, NULL, NULL, NULL);
            hEditW = CreateWindowExA(0, "EDIT", "400", WS_CHILD | WS_VISIBLE | WS_BORDER, 350, 80, 40, 25, hwnd, (HMENU)ID_EDIT_W, NULL, NULL);
            CreateWindowExA(0, "STATIC", "H:", WS_CHILD | WS_VISIBLE, 400, 80, 20, 20, hwnd, NULL, NULL, NULL);
            hEditH = CreateWindowExA(0, "EDIT", "300", WS_CHILD | WS_VISIBLE | WS_BORDER, 420, 80, 40, 25, hwnd, (HMENU)ID_EDIT_H, NULL, NULL);

            CreateWindowExA(0, "BUTTON", "入力値で登録", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 60, 120, 130, 25, hwnd, (HMENU)ID_BTN_SAVE_INPUT, NULL, NULL);
            CreateWindowExA(0, "BUTTON", "現在の枠の位置で登録", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 200, 120, 220, 25, hwnd, (HMENU)ID_BTN_SAVE_CURRENT, NULL, NULL);

            for (const auto& c : configList) { SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)c.name.c_str()); }
            if (!configList.empty()) SendMessage(hCombo, CB_SETCURSEL, 0, 0);
            break;
        }
        case WM_COMMAND: {
            int wmId = LOWORD(wp);
            if (wmId == ID_BTN_APPLY) {
                int idx = SendMessage(hCombo, CB_GETCURSEL, 0, 0);
                if (idx != CB_ERR && idx < configList.size()) {
                    cfg.x = configList[idx].x; cfg.y = configList[idx].y;
                    cfg.w = configList[idx].w; cfg.h = configList[idx].h;
                    RECT winRect = {0, 0, cfg.w, cfg.h};
                    AdjustWindowRect(&winRect, WS_OVERLAPPEDWINDOW, FALSE);
                    SetWindowPos(g_hwnd, NULL, cfg.x + winRect.left, cfg.y + winRect.top, 
                                winRect.right - winRect.left, winRect.bottom - winRect.top, SWP_NOZORDER);
                }
            } else if (wmId == ID_BTN_DELETE) {
                int idx = SendMessage(hCombo, CB_GETCURSEL, 0, 0);
                if (idx == 0) {
                    MessageBoxA(hwnd, "「前回終了時の位置」は削除できません。", "エラー", MB_OK);
                } else if (idx != CB_ERR && idx < configList.size()) {
                    configList.erase(configList.begin() + idx);
                    SaveAllConfigs();
                    SendMessage(hCombo, CB_DELETESTRING, idx, 0);
                    SendMessage(hCombo, CB_SETCURSEL, 0, 0);
                }
            } else if (wmId == ID_BTN_SAVE_INPUT || wmId == ID_BTN_SAVE_CURRENT) {
                char nameBuf[100]; GetWindowTextA(hEditName, nameBuf, sizeof(nameBuf));
                std::string newName = nameBuf;
                for (char& c : newName) if (c == ' ') c = '_'; // スペース回避
                if (newName.empty()) newName = "NoName";

                int nx = 0, ny = 0, nw = 0, nh = 0;
                if (wmId == ID_BTN_SAVE_INPUT) {
                    char buf[32];
                    GetWindowTextA(hEditX, buf, sizeof(buf)); nx = atoi(buf);
                    GetWindowTextA(hEditY, buf, sizeof(buf)); ny = atoi(buf);
                    GetWindowTextA(hEditW, buf, sizeof(buf)); nw = atoi(buf);
                    GetWindowTextA(hEditH, buf, sizeof(buf)); nh = atoi(buf);
                } else {
                    POINT pt = {0, 0}; ClientToScreen(g_hwnd, &pt);
                    RECT rc; GetClientRect(g_hwnd, &rc);
                    nx = pt.x; ny = pt.y; nw = rc.right; nh = rc.bottom;
                }

                if (nw < 50 || nh < 50) { MessageBoxA(hwnd, "幅と高さは50以上にしてください。", "エラー", MB_OK); break; }
                
                bool found = false;
                for (size_t i = 0; i < configList.size(); ++i) {
                    if (configList[i].name == newName) {
                        configList[i].x = nx; configList[i].y = ny; configList[i].w = nw; configList[i].h = nh;
                        found = true; break;
                    }
                }
                if (!found) {
                    configList.push_back({newName, nx, ny, nw, nh});
                    SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)newName.c_str());
                }
                SaveAllConfigs();
                MessageBoxA(hwnd, "保存しました。", "成功", MB_OK);
            }
            break;
        }
        case WM_DESTROY: {
            EnableWindow(g_hwnd, TRUE);
            SetForegroundWindow(g_hwnd);
            break;
        }
        default: return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

// --- ウィンドウプロシージャ ---
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wp;
            RECT rc; GetClientRect(hwnd, &rc);
            HBRUSH brush = CreateSolidBrush(RGB(255, 0, 255)); // ピンク
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);
            return 1;
        }
        case WM_KEYDOWN:
            if (wp == VK_SPACE) { CaptureScreen(hwnd); }
            else if (wp == VK_ESCAPE) { DestroyWindow(hwnd); }
            break;
        case WM_RBUTTONUP: {
            // 右クリックでポップアップメニューを表示
            POINT pt; GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuA(hMenu, MF_STRING, 1001, "保存先の設定");
            AppendMenuA(hMenu, MF_STRING, 1002, "設定 (位置・サイズの保存と呼び出し)");
            AppendMenuA(hMenu, MF_STRING, 1003, "現在の位置・サイズの指定 (シンプル入力)");
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
            break;
        }
        case WM_CREATE: {
            // デフォルトのシステムメニューを取得して、そこに自分のメニューを追加
            HMENU hSysMenu = GetSystemMenu(hwnd, FALSE);
            AppendMenuA(hSysMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hSysMenu, MF_STRING, 1001, "保存先の設定");
            AppendMenuA(hSysMenu, MF_STRING, 1002, "設定 (位置・サイズの保存と呼び出し)");
            AppendMenuA(hSysMenu, MF_STRING, 1003, "現在の位置・サイズの指定 (シンプル入力)");
            break;
        }
        // --- WndProc 内の WM_COMMAND 部分 ---
        case WM_SYSCOMMAND: {
            int wmId = LOWORD(wp);
            if (wmId == 1001) {
                // フォルダ選択ダイアログ (IFileOpenDialog)
                IFileOpenDialog *pfd;
                if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
                    DWORD dwOptions;
                    if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
                        pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                    }
                    if (SUCCEEDED(pfd->Show(hwnd))) {
                        IShellItem *psi;
                        if (SUCCEEDED(pfd->GetResult(&psi))) {
                            PWSTR pszPath;
                            if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                                int len = WideCharToMultiByte(CP_ACP, 0, pszPath, -1, NULL, 0, NULL, NULL);
                                std::string dir(len, 0);
                                WideCharToMultiByte(CP_ACP, 0, pszPath, -1, &dir[0], len, NULL, NULL);
                                if (!dir.empty() && dir.back() == '\0') dir.pop_back();
                                saveDir = dir;
                                SaveSaveDir();
                                CoTaskMemFree(pszPath);
                            }
                            psi->Release();
                        }
                    }
                    pfd->Release();
                }
                
                // ダイアログ終了後にフォーカスを戻す
                SetForegroundWindow(hwnd);
                return 0;
            }
            else if (wmId == 1002) {
                LoadAllConfigs(); // 最新状態を読み込み
                EnableWindow(hwnd, FALSE); // 親ウィンドウの操作を一時停止
                HWND hSub = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, "SubAppClass", "設定 (位置・サイズの保存と呼び出し)",
                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                    CW_USEDEFAULT, CW_USEDEFAULT, 500, 200,
                    hwnd, NULL, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);
                SetForegroundWindow(hSub);
                return 0;
            }
            else if (wmId == 1003) {
                EnableWindow(hwnd, FALSE);
                HWND hSub = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, "InputSubAppClass", "座標指定 (x y w h)",
                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                    CW_USEDEFAULT, CW_USEDEFAULT, 300, 80,
                    hwnd, NULL, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);
                SetForegroundWindow(hSub);
                return 0;
            }
            // 自作メニュー以外はデフォルトの処理に任せる
            return DefWindowProc(hwnd, msg, wp, lp);
        }
        case WM_DESTROY: {
            if (!IsZoomed(hwnd) && !IsIconic(hwnd)){
                POINT pt = {0, 0}; ClientToScreen(hwnd, &pt);
                RECT rc; GetClientRect(hwnd, &rc);
                cfg.x = pt.x; cfg.y = pt.y; cfg.w = rc.right; cfg.h = rc.bottom;
                SaveAllConfigs();
            }
            PostQuitMessage(0);
            break;
        }
        default: return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

// --- メイン関数 ---
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // COMの初期化
    CoInitialize(NULL);

    //DPIスケーリングによる座標のずれを無効化
    SetProcessDPIAware();

    // GDI+のエンジンを起動
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    
    LoadAllConfigs();
    LoadSaveDir();

    const char* CLASS_NAME = "CaptureAppClass";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    //サブウィンドウの登録
    WNDCLASS swc = {};
    swc.lpfnWndProc = SubWndProc;
    swc.hInstance = hInstance;
    swc.lpszClassName = "SubAppClass";
    swc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClass(&swc);

    // シンプル入力用サブウィンドウの登録
    WNDCLASS iwc = {};
    iwc.lpfnWndProc = InputSubWndProc;
    iwc.hInstance = hInstance;
    iwc.lpszClassName = "InputSubAppClass";
    iwc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClass(&iwc);

    RECT winRect = {0, 0, cfg.w, cfg.h};
    AdjustWindowRect(&winRect, WS_OVERLAPPEDWINDOW, FALSE);

    // メインウィンドウをグローバル変数に保存
    g_hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED, CLASS_NAME, "軽量キャプチャ (Space:撮影, 右クリック:メニュー, Esc:終了)",
        WS_OVERLAPPEDWINDOW,
        cfg.x + winRect.left, cfg.y + winRect.top, 
        winRect.right - winRect.left, winRect.bottom - winRect.top,
        NULL, NULL, hInstance, NULL
    );

    SetLayeredWindowAttributes(g_hwnd, RGB(255, 0, 255), 255, LWA_COLORKEY | LWA_ALPHA); // 完全透過（クリックスルー）
    ShowWindow(g_hwnd, nCmdShow);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    // 終了時にGDI+エンジンをシャットダウン
    Gdiplus::GdiplusShutdown(gdiplusToken);
    CoUninitialize();
    return 0;
}