// SaladDashboard.cpp : Defines the entry point for the application.
//
// This is a C++ recreation of the Python Salad API Dashboard using Microsoft Edge WebView2.
// The UI is rendered via embedded HTML/JS/CSS, with native C++ handling API calls and data fetching.
// The HTML is embedded as a resource (IDR_HTML_INDEX) from a separate "index.html" file during build.
// Use a .rc file like this:
// IDR_HTML_INDEX HTML "index.html"
//
// Dependencies:
// - Microsoft.Web.WebView2 NuGet package or SDK
// - Windows Implementation Libraries (WIL)
// - nlohmann/json for JSON handling (include as single header)
// - WinHttp for HTTP requests
//
// Compile with /EHsc /std:c++17 or later.

#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>  // For known folders if needed, but GetTempPath is in windows.h
#include <wil/com.h>
#include <wil/resource.h>
#include <wrl.h>
#include <WebView2.h>  // Add this include for WebView2 interfaces
#include <string>
#include <map>
#include <vector>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <nlohmann/json.hpp>  // Assume nlohmann/json.hpp is included

using json = nlohmann::json;
using namespace Microsoft::WRL;

#define IDR_HTML_INDEX 101  // Define in .rc as HTML "index.html"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")  // If needed

std::wstring utf8_to_wide(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
    if (len == 0) return L"";
    std::wstring wstr(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &wstr[0], len);
    return wstr;
}

std::string wide_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (len == 0) return "";
    std::string str(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &str[0], len, nullptr, nullptr);
    return str;
}

class SaladAPIClient {
public:
    std::string base_url = "https://app-api.salad.com";
    std::string token_path = "C:\\ProgramData\\Salad\\token.txt";
    std::string token;

    SaladAPIClient() {
        token = load_token();
    }

    std::string load_token() {
        std::string t;
        FILE* f = nullptr;
        if (fopen_s(&f, token_path.c_str(), "r") == 0 && f) {
            char buf[1024];
            size_t read = fread(buf, 1, sizeof(buf) - 1, f);
            if (read > 0) {
                buf[read] = '\0';
                t = std::string(buf);
                // Trim whitespace
                t.erase(0, t.find_first_not_of(" \t\n\r\f\v"));
                t.erase(t.find_last_not_of(" \t\n\r\f\v") + 1);
            }
            fclose(f);
        }
        return t;
    }

    json http_get(const std::string& endpoint, bool auth = true) {
        std::map<std::string, std::string> headers;
        headers["X-SRF"] = "1";
        headers["Content-Type"] = "application/json";
        if (auth && !token.empty()) {
            headers["Authorization"] = "Bearer " + token;
        }

        std::string url = base_url + endpoint;
        std::string response = perform_http_get(url, headers);
        if (!response.empty()) {
            try {
                return json::parse(response);
            }
            catch (const json::exception& e) {
                OutputDebugStringA(("JSON parse failed for " + endpoint + ": " + e.what() + "\nResponse: " + response).c_str());
                return json{ {"error", "Parse failed", "details", response} };
            }
        }
        return json{ {"error", "Request failed"} };
    }

    json get_profile() { return http_get("/api/v1/profile"); }
    json get_balance() { return http_get("/api/v1/profile/balance"); }
    json get_earning_history() { return http_get("/api/v2/reports/30-day-earning-history"); }
    json get_machines() { return http_get("/api/v2/machines/"); }
    json get_machine_earnings(const std::string& machine_id, const std::string& timeframe = "30d") {
        return http_get("/api/v2/machines/" + machine_id + "/earning-history?timeframe=" + timeframe);
    }
    json get_machine_5min_earnings(const std::string& machine_id) {
        json res = http_get("/api/v2/machines/" + machine_id + "/earnings/5-minutes");
        if (res.is_number_float()) {
            return json{ {"total", res} };
        }
        return res;
    }
    json get_demand_monitor() { return http_get("/api/v2/demand-monitor/gpu", false); }
    json get_storefront() { return http_get("/api/v2/storefront", false); }

private:
    std::string perform_http_get(const std::string& url_str, const std::map<std::string, std::string>& headers) {
        std::wstring url_wide = utf8_to_wide(url_str);

        URL_COMPONENTS url_comp = {};
        url_comp.dwStructSize = sizeof(url_comp);
        url_comp.dwSchemeLength = (DWORD)-1;
        url_comp.dwHostNameLength = (DWORD)-1;
        url_comp.dwUrlPathLength = (DWORD)-1;
        url_comp.dwExtraInfoLength = (DWORD)-1;

        if (!WinHttpCrackUrl(url_wide.c_str(), 0, 0, &url_comp)) {
            return "";
        }

        std::wstring host(url_comp.lpszHostName, url_comp.dwHostNameLength);
        std::wstring path(url_comp.lpszUrlPath, url_comp.dwUrlPathLength + url_comp.dwExtraInfoLength);

        wil::unique_winhttp_hinternet session(WinHttpOpen(L"SaladDashboard", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0));
        if (!session) return "";

        wil::unique_winhttp_hinternet connect(WinHttpConnect(session.get(), host.c_str(), url_comp.nPort, 0));
        if (!connect) return "";

        wil::unique_winhttp_hinternet request(WinHttpOpenRequest(connect.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, (url_comp.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)));
        if (!request) return "";

        std::wstringstream header_ss;
        for (const auto& h : headers) {
            header_ss << utf8_to_wide(h.first) << L": " << utf8_to_wide(h.second) << L"\r\n";
        }
        std::wstring header_str = header_ss.str();
        if (!header_str.empty()) {
            WinHttpAddRequestHeaders(request.get(), header_str.c_str(), (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }

        if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            return "";
        }

        if (!WinHttpReceiveResponse(request.get(), nullptr)) {
            return "";
        }

        std::string body;
        DWORD size = 0;
        do {
            if (!WinHttpQueryDataAvailable(request.get(), &size)) break;
            if (size == 0) break;

            std::vector<char> buffer(size + 1);
            DWORD downloaded = 0;
            if (WinHttpReadData(request.get(), buffer.data(), size, &downloaded)) {
                body.append(buffer.data(), downloaded);
            }
        } while (size > 0);

        return body;
    }
};

class WebViewWindow {
public:
    WebViewWindow(HINSTANCE hInstance) : hInstance_(hInstance) {}
    ~WebViewWindow() {}

    HRESULT Initialize() {
        HWND hwnd = CreateWindowEx(0, L"SaladDashboardClass", L"Salad API Dashboard", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 1600, 1000, nullptr, nullptr, hInstance_, this);
        if (!hwnd) return E_FAIL;

        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        return CreateWebView(hwnd);
    }

    static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        if (msg == WM_CREATE) {
            LPCREATESTRUCT cs = reinterpret_cast<LPCREATESTRUCT>(lparam);
            WebViewWindow* pThis = static_cast<WebViewWindow*>(cs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
            return pThis->WndProc(hwnd, msg, wparam, lparam);
        }
        WebViewWindow* pThis = reinterpret_cast<WebViewWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        return pThis ? pThis->WndProc(hwnd, msg, wparam, lparam) : DefWindowProc(hwnd, msg, wparam, lparam);
    }

private:
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        switch (msg) {
        case WM_SIZE:
            if (controller_) {
                RECT bounds;
                GetClientRect(hwnd, &bounds);
                controller_->put_Bounds(bounds);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProc(hwnd, msg, wparam, lparam);
    }

    HRESULT CreateWebView(HWND hwnd) {
        // Get temp path for user data folder to avoid corruption issues
        wchar_t tempPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tempPath);
        std::wstring userDataFolder = std::wstring(tempPath) + L"\\SaladDashboardWebView2";

        return CreateCoreWebView2EnvironmentWithOptions(nullptr, userDataFolder.c_str(), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this, hwnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                    if (FAILED(result)) {
                        MessageBoxW(hwnd, L"Failed to create WebView2 environment. Check if WebView2 runtime is installed.", L"Error", MB_OK);
                        return result;
                    }
                    if (!env) return E_FAIL;
                    return env->CreateCoreWebView2Controller(hwnd,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this, hwnd](HRESULT result, ICoreWebView2Controller* ctrl) -> HRESULT {
                                if (FAILED(result)) {
                                    MessageBoxW(hwnd, L"Failed to create WebView2 controller.", L"Error", MB_OK);
                                    return result;
                                }
                                if (!ctrl) return E_FAIL;
                                controller_ = ctrl;
                                wil::com_ptr<ICoreWebView2> webview;
                                HRESULT hr = controller_->get_CoreWebView2(&webview);
                                if (FAILED(hr)) {
                                    MessageBoxW(hwnd, L"Failed to get CoreWebView2.", L"Error", MB_OK);
                                    return hr;
                                }
                                if (!webview) return E_FAIL;
                                webview_ = webview;

                                // Setup event handlers
                                EventRegistrationToken token;
                                hr = webview_->add_WebMessageReceived(
                                    Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                        [this](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                            wil::unique_cotaskmem_string message;
                                            args->get_WebMessageAsJson(&message);
                                            HandleWebMessage(message.get());
                                            return S_OK;
                                        }).Get(), &token);
                                if (FAILED(hr)) {
                                    MessageBoxW(hwnd, L"Failed to add WebMessageReceived handler.", L"Error", MB_OK);
                                    return hr;
                                }

                                // Load HTML from resource
                                std::string html = LoadHtmlResource();
                                if (html.find("Error loading HTML") != std::string::npos) {
                                    MessageBoxW(hwnd, L"Failed to load HTML resource. Check if index.html is added as resource.", L"Error", MB_OK);
                                }
                                std::wstring whtml = utf8_to_wide(html);
                                hr = webview_->NavigateToString(whtml.c_str());
                                if (FAILED(hr)) {
                                    MessageBoxW(hwnd, L"Failed to navigate to HTML string.", L"Error", MB_OK);
                                    return hr;
                                }

                                // Resize
                                RECT bounds;
                                GetClientRect(hwnd, &bounds);
                                controller_->put_Bounds(bounds);

                                return S_OK;
                            }).Get());
                }).Get());
    }

    void HandleWebMessage(const wchar_t* message_wide) {
        if (message_wide == nullptr) return;
        std::string message = wide_to_utf8(std::wstring(message_wide));
        json req;
        try {
            req = json::parse(message);
        }
        catch (const json::exception& e) {
            OutputDebugStringA(("JSON parse error: " + std::string(e.what())).c_str());
            return;
        }

        std::string action = req.value("action", "");
        json response = json::object();
        response["action"] = action;

        if (action == "refresh") {
            response["profile"] = client_.get_profile();
            response["balance"] = client_.get_balance();
            response["earning_history"] = client_.get_earning_history();
            json machines = client_.get_machines();
            if (machines.contains("items")) {
                json items = machines["items"];
                for (auto& item : items) {
                    std::string mid = item.value("machine_id", "");
                    if (!mid.empty()) {
                        json earnings_5min = client_.get_machine_5min_earnings(mid);
                        item["earnings_5min"] = earnings_5min;
                    }
                }
                machines["items"] = items;
            }
            response["machines"] = machines;
        }
        else if (action == "get_demand_monitor") {
            response["data"] = client_.get_demand_monitor();
        }
        else if (action == "get_storefront") {
            response["data"] = client_.get_storefront();
        }
        else if (action == "get_machine_details") {
            std::string mid = req.value("machine_id", "");
            if (!mid.empty()) {
                response["earnings_5min"] = client_.get_machine_5min_earnings(mid);
                response["earnings_30d"] = client_.get_machine_earnings(mid, "30d");
            }
        }
        else if (action == "open_url") {
            std::string url = req.value("url", "");
            if (!url.empty()) {
                ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOW);
            }
            return;  // No response needed
        }

        std::string resp_str = response.dump();
        std::wstring wresp = utf8_to_wide(resp_str);
        webview_->PostWebMessageAsJson(wresp.c_str());
    }

    std::string LoadHtmlResource() {
        HRSRC src = FindResource(hInstance_, MAKEINTRESOURCE(IDR_HTML_INDEX), RT_HTML);
        if (!src) return "<html><body>Error loading HTML resource</body></html>";
        HGLOBAL res = LoadResource(hInstance_, src);
        if (!res) return "<html><body>Error loading HTML resource</body></html>";
        LPVOID data = LockResource(res);
        DWORD size = SizeofResource(hInstance_, src);
        if (size == 0) return "<html><body>Empty HTML resource</body></html>";
        return std::string(static_cast<const char*>(data), size);
    }

    HINSTANCE hInstance_;
    wil::com_ptr<ICoreWebView2Controller> controller_;
    wil::com_ptr<ICoreWebView2> webview_;
    SaladAPIClient client_;
};

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    WNDCLASS wc = {};
    wc.lpfnWndProc = WebViewWindow::WndProcStatic;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"SaladDashboardClass";
    RegisterClass(&wc);

    WebViewWindow window(hInstance);
    if (FAILED(window.Initialize())) {
        MessageBox(nullptr, L"Failed to initialize WebView2. Please install Microsoft Edge WebView2 Runtime from https://developer.microsoft.com/en-us/microsoft-edge/webview2/", L"Error", MB_OK);
        return 1;
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}