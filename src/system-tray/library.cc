#include <napi.h>
#include <windows.h>
#include <strsafe.h>

#include <chrono>
#include <iostream>

using namespace Napi;

void LogImpl(Env env, const char* msg) {
    napi_value args[] = { (napi_value)String::New(env, msg) };
    env.Global().Get("console").As<Object>().Get("log").As<Function>().Call(1, args);
}
void LogImpl(Env env, napi_value obj) {
    napi_value args[] = { obj };
    env.Global().Get("console").As<Object>().Get("log").As<Function>().Call(1, args);
}

#ifdef DEBUG
    #define Log(env, val) LogImpl(env, val)
#else
    #define Log(env, val) 
#endif


char* str(String source) {
    std::string stdStr = source.Utf8Value();
    char* target = new char[stdStr.length() + 1];
    std::strcpy(target, stdStr.c_str());
    return target;
}

class MenuItem {
public:
    PBYTE iconData;
    int iconDataLength;

    char* title;
    char* tooltip;

    char* id;
    UINT _id;

    int itemsCount;
    MenuItem** items;

    MenuItem(PBYTE iconData, int iconDataLength, char* title, char* tooltip, char* id, int itemsCount, MenuItem** items)
        : iconData(iconData), iconDataLength(iconDataLength), title(title), tooltip(tooltip), id(id), itemsCount(itemsCount), items(items)
    {
        _id = -1;
    }

    ~MenuItem() {
        delete iconData;
        delete title;
        delete tooltip;
        delete id;
        for (int i = 0; i < itemsCount; i++)
            delete items[i];
        delete[] items;
        delete items;
    }
};

MenuItem* buildMenuItem(Object object, UINT* nextID) {
    Log(object.Env(), "building menu item");
    Log(object.Env(), object);
    Log(object.Env(), "taking icon");

    PBYTE iconData;
    int iconDataLength;

    if (object.Get("icon").IsBuffer()) {
        Buffer<uint8_t> iconBuffer = object.Get("icon").As<Buffer<uint8_t>>();
        Log(object.Env(), "copying icon data");
        iconDataLength = iconBuffer.Length();
        iconData = new BYTE[iconDataLength];
        memcpy(iconData, iconBuffer.Data(), iconDataLength);
        Log(object.Env(), "copied icon data");
    }
    else {
        Log(object.Env(), "no icon data");
        iconData = NULL;
        iconDataLength = 0;
    }

    Log(object.Env(), "taking title");
    char* title = object.Get("title").IsString() ? str(object.Get("title").As<String>()) : NULL;
    Log(object.Env(), "taking tooltip");
    char* tooltip = object.Get("tooltip").IsString() ? str(object.Get("tooltip").As<String>()) : NULL;
    Log(object.Env(), "taking id");
    char* id = object.Get("id").IsString() ? str(object.Get("id").As<String>()) : NULL;

    Log(object.Env(), "taking items");
    Array itemsArray = object.Get("items").As<Array>();

    int itemsCount;
    MenuItem** items;
    if (itemsArray.IsArray()) {
        Log(object.Env(), "taking items length");
        itemsCount = itemsArray.Length();
        items = new MenuItem* [itemsCount];
        for (int i = 0; i < itemsCount; i++) {
            Log(object.Env(), "building child menu item");
            items[i] = buildMenuItem(itemsArray.Get(i).As<Object>(), nextID);
            Log(object.Env(), "done building child menu item");
        }
    } else {
        Log(object.Env(), "no items");
        itemsCount = 0;
        items = new MenuItem* [0];
    }

    Log(object.Env(), "filling menu item");
    MenuItem* item = new MenuItem(
        iconData, iconDataLength, title, tooltip, id, itemsCount, items
    );
    item->_id = (*nextID)++;
    Log(object.Env(), "done filling menu item");
    Log(object.Env(), "done building menu item");
    return item;
}

thread_local std::function<LRESULT(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam)> FloatingWndProc = NULL;

LRESULT CALLBACK WndProc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam) {
    if (FloatingWndProc) {
        return FloatingWndProc(hWnd, iMsg, wParam, lParam);
    }
    else {
        return DefWindowProc(hWnd, iMsg, wParam, lParam);
    }
}

HWND setupWindow() {
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    WNDCLASS wc;
    HWND hWnd;

    LPCSTR lpszClass = "__hidden__";

    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hbrBackground = nullptr;
    wc.hCursor = nullptr;
    wc.hIcon = nullptr;
    wc.hInstance = hInstance;
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = lpszClass;
    wc.lpszMenuName = nullptr;
    wc.style = 0;
    RegisterClass(&wc);

    return CreateWindow(lpszClass, lpszClass, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr, nullptr, hInstance, nullptr);
}

void appendMenuItems(int itemsCount, MenuItem** items, HMENU menu) {
    for (int i = 0; i < itemsCount; i++) {
        MenuItem* item = items[i];
        if (item->itemsCount == 0) {
            AppendMenu(menu, MF_STRING, item->_id, item->title);
        }
        else {
            HMENU child = CreatePopupMenu();
            appendMenuItems(item->itemsCount, item->items, child);
            AppendMenu(menu, MF_POPUP, (UINT_PTR) child, item->title);
        }

    }
}

char* matchMenuItemId(int itemsCount, MenuItem** items, UINT _id) {
    for (int i = 0; i < itemsCount; i++) {
        MenuItem* item = items[i];
        if (item->_id == _id) {
            return item->id;
        }
        else {
            char* match = matchMenuItemId(item->itemsCount, item->items, _id);
            if (match) {
                return match;
            }
        }
    }
    return NULL;
}

void putNotifyIconNative(
    MenuItem* menu,
    std::function<void(const char*)> error,
    std::function<void()> ready,
    std::function<void(const char*)> click,
    std::function<void()> end
) {
    HICON hIcon;
    if (menu->iconData) {
        hIcon = CreateIconFromResourceEx(menu->iconData, menu->iconDataLength, TRUE, 0x30000, 0, 0, LR_DEFAULTCOLOR);
    }
    else {
        hIcon = NULL;
    }

    HWND hWnd;
    if (hWnd = setupWindow())
    {
        // SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)&WindowProc);

        const UINT notifyCallbackMessage = WM_APP + 1;
        NOTIFYICONDATA nid = {};

        nid.cbSize = sizeof(nid);
        nid.hWnd = hWnd;
        nid.uFlags = NIF_ICON | NIF_TIP | NIF_INFO | NIF_SHOWTIP | NIF_MESSAGE;
        nid.uVersion = NOTIFYICON_VERSION_4;
        nid.hIcon = hIcon;
        nid.uCallbackMessage = notifyCallbackMessage;

        if (menu->title) {
            StringCchCopy(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), menu->title);
        }
        if (menu->tooltip) {
            StringCchCopy(nid.szTip, ARRAYSIZE(nid.szTip), menu->tooltip);
        }

        if (Shell_NotifyIcon(NIM_ADD, &nid)) {
            Shell_NotifyIcon(NIM_SETVERSION, &nid);

            FloatingWndProc = [notifyCallbackMessage, click, menu](HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
                if (iMsg == notifyCallbackMessage) {
                    if (lParam == WM_LBUTTONUP || lParam == WM_RBUTTONUP) {
                        click("#");

                        // Create an empty popup menu
                        HMENU popMenu = CreatePopupMenu();

                        appendMenuItems(menu->itemsCount, menu->items, popMenu);

                        // Get the position of the cursor
                        POINT pCursor;

                        GetCursorPos(&pCursor);
                        // Popup the menu with cursor position as the coordinates to pop it up

                        SetForegroundWindow(hWnd);
                        WORD menuID = TrackPopupMenu(popMenu, TPM_LEFTBUTTON | TPM_RETURNCMD, pCursor.x, pCursor.y, 0, hWnd, NULL);

                        char* id = matchMenuItemId(menu->itemsCount, menu->items, menuID);

                        click(id);

                        return 0;
                    }
                }

                return DefWindowProc(hWnd, iMsg, wParam, lParam);
            };

            ready();

            BOOL bRet;

            MSG msg;
            while ((bRet = GetMessage(&msg, hWnd, 0, 0)) != 0)
            {
                if (bRet == -1)
                {
                    break;
                }
                else
                {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
            error("end of message query");
        }
        else {
            error("couldn't notify icon");
        }
    }
    else {
        error("couldn't create window");
    }
}

void putNotifyIcon(const CallbackInfo& info) {
    Env env = info.Env();

    Object trayItem = info[0].As<Object>();
    UINT idCounter = 2000;
    MenuItem* menuItem = buildMenuItem(trayItem, &idCounter);

    std::thread windowThread;

    ThreadSafeFunction* callback = (ThreadSafeFunction*) malloc(sizeof(ThreadSafeFunction));
    *callback = ThreadSafeFunction::New(
        env,
        info[1].As<Function>(),
        "system_tray native putSytemTray callback",
        0,
        1,
        [&windowThread](Env env) {
            windowThread.join();
        }
    );

    Log(env, "creating window thread");
    windowThread = std::thread(
        [callback, menuItem] {
            putNotifyIconNative(
                menuItem,
                [callback](const char* error) {
                    callback->BlockingCall(error, [] (Env env, Function callback, const char* error) {
                        Object action = Object::New(env);
                        action.Set("error", String::New(env, error));
                        callback.Call({ action });
                    });
                },
                [callback]() {
                    callback->BlockingCall([] (Env env, Function callback) {
                        Object action = Object::New(env);
                        action.Set("ready", String::New(env, ""));
                        callback.Call({ action });
                    });
                },
                [callback](const char* id) {
                    callback->BlockingCall(id, [] (Env env, Function callback, const char* id) {
                        Object action = Object::New(env);
                        action.Set("click", String::New(env, id == NULL ? "?" : id));
                        callback.Call({ action });
                    });
                },
                [callback]() {
                    callback->BlockingCall([](Env env, Function callback) {
                        Object action = Object::New(env);
                        action.Set("end", String::New(env, ""));
                        callback.Call({ action });
                    });
                }
            );
            delete menuItem;
            callback->Release();
            delete callback;
        }
    );
    Log(env, "detaching window thread");
    windowThread.detach();
    Log(env, "done putting notify icon");
}

Object Init(Env env, Object exports) {
    exports.Set(
        "putNotifyIcon",
        Function::New(env, putNotifyIcon)
    );
    return exports;
}

NODE_API_MODULE(system_tray, Init)
