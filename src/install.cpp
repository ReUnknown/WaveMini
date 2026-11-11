#include "install.h"
#include "app.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace {

const wchar_t* UninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\WaveMini";

wstring selfPath() {
    wchar_t path[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH * 2);
    return path;
}

wstring folder(const wchar_t* variable, const wchar_t* under) {
    wchar_t base[MAX_PATH] = {};
    GetEnvironmentVariableW(variable, base, MAX_PATH);
    return wstring(base) + under;
}

wstring installDir() { return folder(L"LOCALAPPDATA", L"\\Programs\\WaveMini"); }
wstring shortcutPath() { return folder(L"APPDATA", L"\\Microsoft\\Windows\\Start Menu\\Programs\\WaveMini.lnk"); }

// Asks a running WaveMini to close, and waits for it to be gone.
void closeRunning() {
    for (int i = 0; i < 40; i++) {
        HWND other = FindWindowW(L"WaveMiniWindow", nullptr);
        if (!other) return;
        if (i == 0) PostMessageW(other, WM_CLOSE, 0, 0);
        Sleep(100);
    }
}

void setValue(HKEY key, const wchar_t* name, const wstring& value) {
    RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
}

bool makeShortcut(const wstring& target, const wstring& at) {
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&link))) return false;
    link->SetPath(target.c_str());
    link->SetDescription(L"WaveMini");
    IPersistFile* file = nullptr;
    bool ok = SUCCEEDED(link->QueryInterface(IID_IPersistFile, (void**)&file)) && SUCCEEDED(file->Save(at.c_str(), TRUE));
    if (file) file->Release();
    link->Release();
    return ok;
}

int doInstall() {
    wstring dir = installDir(), target = dir + L"\\WaveMini.exe", self = selfPath();
    closeRunning();
    CreateDirectoryW(folder(L"LOCALAPPDATA", L"\\Programs").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    bool copied = false;
    for (int i = 0; i < 20 && !copied; i++) {
        copied = CopyFileW(self.c_str(), target.c_str(), FALSE) != 0;
        if (!copied) Sleep(150);
    }
    if (!copied) {
        MessageBoxW(nullptr, L"WaveMini couldn't be installed: its folder can't be written to.", L"WaveMini", MB_OK | MB_ICONWARNING);
        return 1;
    }
    makeShortcut(target, shortcutPath());
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, UninstallKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        setValue(key, L"DisplayName", L"WaveMini");
        setValue(key, L"DisplayVersion", W(WAVEMINI_VERSION));
        setValue(key, L"Publisher", L"ReUnknown");
        setValue(key, L"DisplayIcon", target);
        setValue(key, L"InstallLocation", dir);
        setValue(key, L"UninstallString", L"\"" + target + L"\" --uninstall");
        DWORD one = 1;
        RegSetValueExW(key, L"NoModify", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
        RegSetValueExW(key, L"NoRepair", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
        RegCloseKey(key);
    }
    ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
    return 0;
}

int doUninstall() {
    if (MessageBoxW(nullptr, L"Remove WaveMini from this PC?\n\nYour sign-in and settings on this PC are removed too.", L"WaveMini", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
    closeRunning();
    DeleteFileW(shortcutPath().c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, UninstallKey);
    HKEY run;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &run) == ERROR_SUCCESS) {
        RegDeleteValueW(run, L"WaveMini");
        RegCloseKey(run);
    }
    // This program can't delete itself while it runs: a moment after it exits, the folders go.
    wstring command = L"cmd.exe /c ping -n 3 127.0.0.1 >nul & rmdir /s /q \"" + installDir() + L"\" & rmdir /s /q \"" + dataDir() + L"\"";
    STARTUPINFOW start = {};
    start.cb = sizeof(start);
    PROCESS_INFORMATION process = {};
    std::vector<wchar_t> line(command.begin(), command.end());
    line.push_back(0);
    wchar_t temp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp);
    if (CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, temp, &start, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    return 0;
}

// "0.2.10" against "0.2.9": which is newer, number by number.
bool newer(const string& candidate, const string& than) {
    auto parts = [](string v) {
        if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v = v.substr(1);
        std::vector<int> out;
        for (auto& p : split(v, ".")) out.push_back(atoi(p.c_str()));
        return out;
    };
    std::vector<int> a = parts(candidate), b = parts(than);
    for (size_t i = 0; i < std::max(a.size(), b.size()); i++) {
        int x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
        if (x != y) return x > y;
    }
    return false;
}

struct Outcome { int result = 0; string version; };   // 0 couldn't check, 1 up to date, 2 swapped in, 3 couldn't swap

Outcome fetchUpdate() {
    Outcome out;
    // The newest release, betas included (GitHub's "latest" leaves those out).
    HttpRes res = http("GET", "https://api.github.com/repos/ReUnknown/WaveMini/releases?per_page=1", {{"User-Agent", "WaveMini"}, {"Accept", "application/vnd.github+json"}});
    if (!res.ok()) return out;
    Json list = Json::parse(res.body);
    const Json& release = list[0];
    out.version = release["tag_name"].str();
    if (out.version.empty()) return out;
    if (!newer(out.version, WAVEMINI_VERSION)) { out.result = 1; return out; }
    string url;
    long long size = 0;
    for (auto& asset : release["assets"].each()) {
        if (asset["name"].str() != "WaveMini.exe") continue;
        url = asset["browser_download_url"].str();
        size = (long long)asset["size"].num();
    }
    out.result = 3;
    if (url.empty() || size < 100000) return out;
    wstring self = selfPath(), fresh = self + L".new", old = self + L".old";
    HANDLE file = CreateFileW(fresh.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return out;
    long long got = 0;
    bool program = false;
    HttpRes download = http("GET", url, {{"User-Agent", "WaveMini"}, {"Accept", "application/octet-stream"}}, "", [&](const char* data, size_t count) {
        if (got == 0 && count >= 2) program = data[0] == 'M' && data[1] == 'Z';
        DWORD wrote = 0;
        if (!WriteFile(file, data, (DWORD)count, &wrote, nullptr)) return false;
        got += wrote;
        return true;
    });
    CloseHandle(file);
    // The whole file, and a Windows program: only then does it replace this one.
    if (!download.ok() || got != size || !program) { DeleteFileW(fresh.c_str()); return out; }
    DeleteFileW(old.c_str());
    if (!MoveFileExW(self.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(fresh.c_str()); return out; }
    if (!MoveFileExW(fresh.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING)) { MoveFileExW(old.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING); return out; }
    out.result = 2;
    return out;
}

}   // namespace

bool install::handle(int argc, wchar_t** argv) {
    wstring first = argc >= 2 ? argv[1] : L"";
    wstring name = selfPath();
    size_t slash = name.find_last_of(L"\\/");
    if (slash != wstring::npos) name = name.substr(slash + 1);
    for (auto& c : name) c = (wchar_t)towlower(c);
    if (first == L"--uninstall") { doUninstall(); return true; }
    if (first == L"--install" || name.find(L"setup") != wstring::npos) { doInstall(); return true; }
    return false;
}

void install::cleanUp() {
    wstring self = selfPath();
    DeleteFileW((self + L".old").c_str());
    DeleteFileW((self + L".new").c_str());
}

void install::checkForUpdate(bool byUser) {
    if (byUser) showToast("Checking for updates...");
    async<Outcome>(fetchUpdate, [byUser](Outcome outcome) {
        if (outcome.result == 2) {
            if (!byUser) { showToast("WaveMini " + outcome.version + " is ready: it opens next time"); return; }
            // Straight into the new one.
            ShellExecuteW(nullptr, L"open", selfPath().c_str(), L"--after-update", nullptr, SW_SHOWNORMAL);
            windowClose();
            return;
        }
        if (!byUser) return;
        showToast(outcome.result == 1 ? "You have the latest version" : outcome.result == 3 ? "Couldn't download the update" : "Couldn't check for updates");
    });
}

void install::checkDaily() {
    wstring marker = dataDir() + L"\\update-check";
    WIN32_FILE_ATTRIBUTE_DATA info = {};
    if (GetFileAttributesExW(marker.c_str(), GetFileExInfoStandard, &info)) {
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER a, b;
        a.LowPart = now.dwLowDateTime; a.HighPart = now.dwHighDateTime;
        b.LowPart = info.ftLastWriteTime.dwLowDateTime; b.HighPart = info.ftLastWriteTime.dwHighDateTime;
        if (a.QuadPart - b.QuadPart < 24ULL * 3600 * 10000000) return;
    }
    writeFile(marker, "1");
    checkForUpdate(false);
}
