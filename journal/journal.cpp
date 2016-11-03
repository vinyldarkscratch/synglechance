#include <windows.h>

#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 600
#define IN_BUFFER_SIZE 256

static const WCHAR journal_classname[] = L"journal";

static HBITMAP image_handle = NULL;
static HANDLE image_mutex = NULL;

static HWND window = NULL;

static void loadImage(const char *name) {
    if (image_handle) {
        DeleteObject(image_handle);
    }
    image_handle = LoadBitmapA(GetModuleHandleW(NULL), name);
}

// Window procedure
LRESULT CALLBACK journal_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc;
        HDC hdcMem;

        WaitForSingleObject(image_mutex, INFINITE);
        hdc = BeginPaint(hwnd, &ps);
        hdcMem = CreateCompatibleDC(hdc);
        HANDLE oldBitmap = SelectObject(hdcMem, image_handle);
        BitBlt(hdc, 0, 0, DEFAULT_WIDTH, DEFAULT_HEIGHT, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, oldBitmap);
        DeleteDC(hdcMem);
        EndPaint(hwnd, &ps);
        ReleaseMutex(image_mutex);
    } break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// IPC thread
#include <stdio.h>
DWORD WINAPI ipc_thread(LPVOID lpParam)
{
    (void)lpParam;

    char message[IN_BUFFER_SIZE];
    DWORD len;

    HANDLE pipe = CreateNamedPipeW(L"\\\\.\\pipe\\oneshot-game-to-journal",
                                   PIPE_ACCESS_INBOUND,
                                   PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                   PIPE_UNLIMITED_INSTANCES,
                                   IN_BUFFER_SIZE,
                                   IN_BUFFER_SIZE,
                                   0,
                                   NULL);

    for (;;) {
        if (!ConnectNamedPipe(pipe, NULL))
            continue;
        for (;;) {
            if (ReadFile(pipe, (void*)message, IN_BUFFER_SIZE, &len, NULL)
                    && len == IN_BUFFER_SIZE)
            {
                WaitForSingleObject(image_mutex, INFINITE);
                if (strcmp(message, "default") == 0) {
                    exit(0);
                }
                loadImage(message);
                InvalidateRect(window, NULL, FALSE);
                ReleaseMutex(image_mutex);
            } else {
                break;
            }
        }
        DisconnectNamedPipe(pipe);
    }

    CloseHandle(pipe);

    return 0;
}

int do_journal()
{
    // Create
    HINSTANCE module = GetModuleHandleW(NULL);

    loadImage("default");

    // Initial image
    char message[IN_BUFFER_SIZE];
    DWORD len;
    HANDLE pipe = CreateFileW(L"\\\\.\\pipe\\oneshot-journal-to-game",
	                          GENERIC_READ,
	                          0,
	                          NULL,
	                          OPEN_EXISTING,
	                          0,
	                          NULL);
    if (pipe != INVALID_HANDLE_VALUE) {
        if (ReadFile(pipe, (void*)message, IN_BUFFER_SIZE, &len, NULL)
                && len == IN_BUFFER_SIZE)
        {
            loadImage(message);
        }
        CloseHandle(pipe);
    }

    WNDCLASSEXW wc;
    wc.cbSize           = sizeof(wc);
    wc.style            = 0;
    wc.lpfnWndProc      = journal_proc;
    wc.cbClsExtra       = 0;
    wc.cbWndExtra       = 0;
    wc.hInstance        = module;
    wc.hIcon            = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor          = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground    = NULL;
    wc.lpszMenuName     = NULL;
    wc.lpszClassName    = journal_classname;
    wc.hIconSm          = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&wc))
        return 1;

    if (!(window = CreateWindowExW(WS_EX_COMPOSITED | WS_EX_LAYERED | WS_EX_TOPMOST,
                                   journal_classname,
                                   L"",
                                   WS_OVERLAPPEDWINDOW ^ (WS_THICKFRAME | WS_MAXIMIZEBOX),
                                   CW_USEDEFAULT, CW_USEDEFAULT,
                                   DEFAULT_WIDTH, DEFAULT_HEIGHT,
                                   NULL, NULL, module, NULL))) {
        return 1;
    }

    SetLayeredWindowAttributes(window, RGB(0, 255, 0), 0, LWA_COLORKEY);

    ShowWindow(window, SW_SHOWNOACTIVATE);
    UpdateWindow(window);

    image_mutex = CreateMutexW(NULL, NULL, NULL);
    HANDLE thread = CreateThread(NULL, 0, ipc_thread, NULL, 0, NULL);

    // Dispatch messages
    for (;;) {
        MSG msg;
        if (GetMessage(&msg, window, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            break;
        }
    }

    // TODO make this less messy?
    TerminateThread(thread, 0);

    DestroyWindow(window);

    return 0;
}
