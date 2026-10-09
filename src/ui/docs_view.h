#pragma once

#include <windows.h>
#include <vector>
#include <string>

namespace sk {

struct ClassInfo {
    const char* className;
    const char* category;
    const char* shortDesc;
    const char* longDesc;
};

class DocsView {
public:
    bool create(HWND parent, int x, int y, int w, int h);
    void destroy();
    void setVisible(bool visible);
    void resize(int x, int y, int w, int h);
    void selectClass(int index);
    int selectedIndex() const { return sel_; }

private:
    HWND hwnd_{nullptr};
    HWND list_{nullptr};
    HWND detailEdit_{nullptr};
    HWND propsHeader_{nullptr};
    HWND propsList_{nullptr};
    int sel_{-1};
    std::vector<ClassInfo> classes_;
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void buildClassList();
    void updateDetails();
};

} // namespace sk
