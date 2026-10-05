#include "code_view.h"

#include <windows.h>
#include <commctrl.h>

#include <string>

#include "../core/log.h"
#include "../project/project.h"
#include "../project/scripts.h"
#include "dark_button.h"
#include "prompt.h"
#include "theme.h"
#include "workspace_tabs.h"

namespace sk {

namespace {

const char* kPanelClass = "MotorSKCodePanel";

constexpr int kHeaderHeight = 36;

enum ControlId {
    kIdNewScript = 1,
    kIdNewFolder = 2,
    kIdRename = 3,
    kIdDelete = 4,
    kIdStatus = 100,
    kIdNameEdit = 200,
};

bool endsWithSk(const std::string& s) {
    return s.size() > 3 && s.compare(s.size() - 3, 3, ".sk") == 0;
}

std::string lastSegment(const std::string& rel) {
    const std::string::size_type slash = rel.rfind('/');
    return slash == std::string::npos ? rel : rel.substr(slash + 1);
}

std::string parentSegment(const std::string& rel) {
    const std::string::size_type slash = rel.rfind('/');
    return slash == std::string::npos ? "" : rel.substr(0, slash);
}

bool isDirRel(const std::string& rel) {
    return !endsWithSk(lastSegment(rel));
}

std::string itemRel(HWND tree, HTREEITEM item) {
    TVITEMA ti{};
    ti.hItem = item;
    ti.mask = TVIF_PARAM;
    if (!TreeView_GetItem(tree, &ti)) return "";
    auto* text = reinterpret_cast<std::string*>(ti.lParam);
    return text ? *text : "";
}

HTREEITEM findRel(HWND tree, HTREEITEM item, const std::string& rel) {
    while (item) {
        if (itemRel(tree, item) == rel) return item;
        if (HTREEITEM child = TreeView_GetChild(tree, item)) {
            if (HTREEITEM found = findRel(tree, child, rel)) return found;
        }
        item = TreeView_GetNextSibling(tree, item);
    }
    return nullptr;
}

void setBtnEnabled(void* handle, bool enabled) {
    HWND hwnd = static_cast<HWND>(handle);
    if (!hwnd) return;
    if (!enabled) {
        if (ui::DarkButton* b = ui::findDarkButton(hwnd); b && b->hover) {
            b->hover = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
    EnableWindow(hwnd, enabled);
}

void paintPanelHeader(HDC hdc, const RECT& rc, const char* title) {
    RECT header{0, 0, rc.right, kHeaderHeight};
    FillRect(hdc, &header, theme::headerBackgroundBrush());

    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, theme::uiHeaderFont()));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, theme::text());
    RECT textRc{12, 0, rc.right, kHeaderHeight};
    DrawTextA(hdc, title, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);

    HPEN pen = CreatePen(PS_SOLID, 1, theme::border());
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    MoveToEx(hdc, 0, kHeaderHeight, nullptr);
    LineTo(hdc, rc.right, kHeaderHeight);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void insertChildren(HWND tree, HTREEITEM parent, const Project& p,
                    const std::string& rel) {
    for (const scripts::Node& child : scripts::list(p, rel)) {
        TVINSERTSTRUCTA ins{};
        ins.hParent = parent;
        ins.hInsertAfter = TVI_LAST;
        ins.item.mask = TVIF_TEXT | TVIF_PARAM;
        ins.item.pszText = const_cast<char*>(child.name.c_str());
        ins.item.lParam = reinterpret_cast<LONG_PTR>(new std::string(child.rel));
        HTREEITEM h = TreeView_InsertItem(tree, &ins);
        if (child.isDir) {
            insertChildren(tree, h, p, child.rel);
            TreeView_Expand(tree, h, TVE_EXPAND);
        }
    }
}

void addTreeRootsAndChildren(HWND tree, const Project& p) {
    for (const char* root : scripts::kRoots) {
        TVINSERTSTRUCTA ins{};
        ins.hParent = TVI_ROOT;
        ins.hInsertAfter = TVI_LAST;
        ins.item.mask = TVIF_TEXT | TVIF_PARAM;
        ins.item.pszText = const_cast<char*>(root);
        ins.item.lParam = reinterpret_cast<LONG_PTR>(new std::string(root));
        HTREEITEM hRoot = TreeView_InsertItem(tree, &ins);

        insertChildren(tree, hRoot, p, root);
        TreeView_Expand(tree, hRoot, TVE_EXPAND);
    }
}

} // namespace

bool CodeView::create(void* parentHwnd) {
    parent_ = parentHwnd;

    static bool registered = false;
    if (!registered) {
        HINSTANCE inst = GetModuleHandleW(nullptr);

        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&CodeView::wndProc);
        wc.hInstance = inst;
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.hbrBackground = theme::backgroundBrush();
        wc.lpszClassName = kPanelClass;
        RegisterClassExA(&wc);

        INITCOMMONCONTROLSEX icc{sizeof(icc),
                                 ICC_TREEVIEW_CLASSES | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&icc);

        registered = true;
    }

    HWND parent = static_cast<HWND>(parent_);
    RECT rc{};
    GetClientRect(parent, &rc);
    HINSTANCE inst = GetModuleHandleW(nullptr);

    scriptsPanel_ = CreateWindowExA(0, kPanelClass, "Scripts",
                                    WS_CHILD | WS_CLIPCHILDREN,
                                    0, 0, kTreeWidth, 100, parent, nullptr,
                                    inst, this);
    propsPanel_ = CreateWindowExA(0, kPanelClass, "Properties",
                                  WS_CHILD | WS_CLIPCHILDREN,
                                  0, 0, kPropsWidth, 100, parent, nullptr,
                                  inst, this);
    if (!scriptsPanel_ || !propsPanel_) {
        SK_ERROR("CodeView: CreateWindowEx fallo (error %lu)", GetLastError());
        return false;
    }

    HWND panel = static_cast<HWND>(scriptsPanel_);

    struct {
        const char* text;
        int id;
        int x, w;
    } const buttons[] = {
        {"Script", kIdNewScript, 8, 56},
        {"Carpeta", kIdNewFolder, 70, 80},
        {"Renombrar", kIdRename, 156, 92},
        {"Borrar", kIdDelete, 254, 64},
    };
    void** btnSlots[] = {&btnNewScript_, &btnNewFolder_, &btnRename_, &btnDelete_};
    for (int i = 0; i < 4; ++i) {
        HWND b = CreateWindowExA(0, "BUTTON", buttons[i].text,
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                 buttons[i].x, 42, buttons[i].w, 28, panel,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(buttons[i].id)),
                                 inst, nullptr);
        SendMessageA(b, WM_SETFONT, reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
        ui::makeDarkButton(b);
        *btnSlots[i] = b;
    }

    HWND tree = CreateWindowExA(0, WC_TREEVIEWA, "",
                                WS_CHILD | WS_VISIBLE |
                                TVS_HASBUTTONS | TVS_HASLINES |
                                TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                                8, 78, kTreeWidth - 16, 100, panel,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(10)),
                                inst, nullptr);
    tree_ = tree;
    SendMessageA(tree, WM_SETFONT, reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    TreeView_SetBkColor(tree, theme::listBackground());
    TreeView_SetTextColor(tree, theme::text());
    TreeView_SetLineColor(tree, theme::border());

    // Panel izquierdo: etiquetas fijas + valores del nodo seleccionado
    // (el nombre va en un EDIT de solo lectura).
    HWND props = static_cast<HWND>(propsPanel_);
    struct Field {
        void** slot;       // donde guardar el handle si hace falta
        const char* cls;
        const char* text;
        int id;
        int x, y, w, h;
        bool clientEdge;
        HFONT font;
    };
    const Field fields[] = {
        {&propsStatus_, "STATIC", "Sin seleccion", kIdStatus,
         12, 44, 276, 20, false, theme::uiFont()},
        {nullptr, "STATIC", "Nombre", 0,
         12, 76, 76, 22, false, theme::uiFont()},
        {&propsName_, "EDIT", "", kIdNameEdit,
         94, 74, 194, 26, true, theme::uiFont()},
        {nullptr, "STATIC", "Tipo", 0,
         12, 110, 76, 22, false, theme::uiFont()},
        {&propsType_, "STATIC", "-", 0,
         94, 112, 194, 20, false, theme::uiFont()},
        {nullptr, "STATIC", "Ubicacion", 0,
         12, 144, 76, 22, false, theme::uiFont()},
        {&propsLoc_, "STATIC", "-", 0,
         94, 146, 194, 20, false, theme::uiFont()},
        {nullptr, "STATIC", "Asociado a", 0,
         12, 178, 76, 22, false, theme::uiFont()},
        {&propsAssoc_, "STATIC", "-", 0,
         94, 180, 194, 20, false, theme::uiFont()},
    };
    for (const Field& f : fields) {
        HWND h = CreateWindowExA(f.clientEdge ? WS_EX_CLIENTEDGE : 0,
                                 f.cls, f.text, WS_CHILD | WS_VISIBLE |
                                 (f.cls[0] == 'E'
                                      ? (DWORD)(ES_AUTOHSCROLL | ES_READONLY | WS_TABSTOP | WS_BORDER)
                                      : (DWORD)SS_LEFT),
                                 f.x, f.y, f.w, f.h, props,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(f.id)),
                                 inst, nullptr);
        SendMessageA(h, WM_SETFONT, reinterpret_cast<WPARAM>(f.font), TRUE);
        if (f.slot) *f.slot = h;
    }

    resize(rc.right, rc.bottom);
    SK_INFO("Division CODE lista (organizador de scripts)");
    return true;
}

void CodeView::destroy() {
    project_ = nullptr;
    if (scriptsPanel_) {
        DestroyWindow(static_cast<HWND>(scriptsPanel_));
        scriptsPanel_ = nullptr;
    }
    if (propsPanel_) {
        DestroyWindow(static_cast<HWND>(propsPanel_));
        propsPanel_ = nullptr;
    }
    tree_ = nullptr;
    btnNewScript_ = btnNewFolder_ = btnRename_ = btnDelete_ = nullptr;
    propsStatus_ = propsName_ = propsType_ = propsLoc_ = propsAssoc_ = nullptr;
}

void CodeView::open(const Project& project) {
    project_ = &project;
    scripts::ensureFolders(project);
    rebuildTree();
}

void CodeView::close() {
    project_ = nullptr;
    if (tree_) TreeView_DeleteAllItems(static_cast<HWND>(tree_));
}

void CodeView::setVisible(bool visible) {
    visible_ = visible;
    if (scriptsPanel_) ShowWindow(static_cast<HWND>(scriptsPanel_), visible ? SW_SHOW : SW_HIDE);
    if (propsPanel_) ShowWindow(static_cast<HWND>(propsPanel_), visible ? SW_SHOW : SW_HIDE);
}

void CodeView::resize(int width, int height) {
    layoutPanels(width, height);
}

void CodeView::layoutPanels(int width, int height) {
    if (!scriptsPanel_ || !propsPanel_) return;
    const int y = WorkspaceTabs::kTopBandHeight;
    int panelH = height - y;
    if (panelH < 0) panelH = 0;

    MoveWindow(static_cast<HWND>(propsPanel_), 0, y, kPropsWidth, panelH, TRUE);
    MoveWindow(static_cast<HWND>(scriptsPanel_),
               width - kTreeWidth, y, kTreeWidth, panelH, TRUE);

    if (tree_) {
        MoveWindow(static_cast<HWND>(tree_), 8, 78,
                   kTreeWidth - 16, panelH - 78 - 8, TRUE);
    }
}

void CodeView::rebuildTree() {
    if (!tree_ || !project_) return;
    HWND tree = static_cast<HWND>(tree_);

    const std::string keep = selectedRel();
    TreeView_DeleteAllItems(tree);
    addTreeRootsAndChildren(tree, *project_);

    if (!keep.empty()) {
        selectRel(keep);
    }
    updateProperties();
    updateButtons();
}

std::string CodeView::selectedRel() const {
    if (!tree_) return "";
    HWND tree = static_cast<HWND>(tree_);
    HTREEITEM item = TreeView_GetSelection(tree);
    if (!item) return "";
    return itemRel(tree, item);
}

void CodeView::selectRel(const std::string& rel) {
    if (!tree_ || rel.empty()) return;
    HWND tree = static_cast<HWND>(tree_);
    if (HTREEITEM item = findRel(tree, TreeView_GetRoot(tree), rel)) {
        TreeView_SelectItem(tree, item);
        TreeView_EnsureVisible(tree, item);
    }
}

std::string CodeView::parentForNew() const {
    const std::string sel = selectedRel();
    if (sel.empty()) return scripts::kRoots[0];
    if (isDirRel(sel)) return sel;
    return parentSegment(sel);
}

void CodeView::updateProperties() {
    if (!propsName_) return;
    const std::string sel = selectedRel();

    if (sel.empty()) {
        SetWindowTextA(static_cast<HWND>(propsStatus_), "Sin seleccion");
        SetWindowTextA(static_cast<HWND>(propsName_), "");
        SetWindowTextA(static_cast<HWND>(propsType_), "-");
        SetWindowTextA(static_cast<HWND>(propsLoc_), "-");
        SetWindowTextA(static_cast<HWND>(propsAssoc_), "-");
        return;
    }

    const std::string name = lastSegment(sel);
    const std::string parent = parentSegment(sel);

    SetWindowTextA(static_cast<HWND>(propsStatus_), "");
    SetWindowTextA(static_cast<HWND>(propsName_), name.c_str());
    const char* type = scripts::isProtected(sel) ? "Categoria raiz"
                       : isDirRel(sel)          ? "Carpeta"
                                                : "Script";
    SetWindowTextA(static_cast<HWND>(propsType_), type);
    const std::string loc = parent.empty() ? "scripts" : "scripts/" + parent;
    SetWindowTextA(static_cast<HWND>(propsLoc_), loc.c_str());
    SetWindowTextA(static_cast<HWND>(propsAssoc_), "-");
}

void CodeView::updateButtons() {
    const std::string sel = selectedRel();
    const bool editable = !sel.empty() && !scripts::isProtected(sel);
    setBtnEnabled(btnRename_, editable);
    setBtnEnabled(btnDelete_, editable);
}

void CodeView::onNewScript() {
    if (!project_) return;
    const std::string parent = parentForNew();
    const std::string typed =
        ui::promptText(static_cast<HWND>(scriptsPanel_), "Nuevo script", "");
    if (typed.empty()) return;

    std::string base = typed;
    if (endsWithSk(base)) base.erase(base.size() - 3);
    if (base.empty() || !project::isValidName(base)) {
        MessageBoxA(static_cast<HWND>(scriptsPanel_), "El nombre no es valido.",
                    "Nuevo script", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!scripts::createScript(*project_, parent, base)) {
        MessageBoxA(static_cast<HWND>(scriptsPanel_),
                    "No se pudo crear el script.\n¿Ya existe un elemento con ese nombre?",
                    "Nuevo script", MB_OK | MB_ICONERROR);
        return;
    }
    rebuildTree();
    selectRel(parent.empty() ? base + ".sk" : parent + "/" + base + ".sk");
    updateProperties();
}

void CodeView::onNewFolder() {
    if (!project_) return;
    const std::string parent = parentForNew();
    const std::string typed =
        ui::promptText(static_cast<HWND>(scriptsPanel_), "Nueva carpeta", "");
    if (typed.empty()) return;

    if (scripts::isBadName(typed)) {
        MessageBoxA(static_cast<HWND>(scriptsPanel_), "El nombre no es valido.",
                    "Nueva carpeta", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!scripts::createFolder(*project_, parent, typed)) {
        MessageBoxA(static_cast<HWND>(scriptsPanel_),
                    "No se pudo crear la carpeta.\n¿Ya existe un elemento con ese nombre?",
                    "Nueva carpeta", MB_OK | MB_ICONERROR);
        return;
    }
    rebuildTree();
    selectRel(parent.empty() ? typed : parent + "/" + typed);
    updateProperties();
}

void CodeView::onRename() {
    if (!project_) return;
    const std::string sel = selectedRel();
    if (sel.empty() || scripts::isProtected(sel)) return;

    const std::string oldName = lastSegment(sel);
    const bool script = endsWithSk(oldName);
    const std::string initial = script ? oldName.substr(0, oldName.size() - 3) : oldName;

    const std::string typed =
        ui::promptText(static_cast<HWND>(scriptsPanel_), "Renombrar", initial.c_str());
    if (typed.empty() || typed == initial) return;

    std::string base = typed;
    if (script && endsWithSk(base)) base.erase(base.size() - 3);
    if (base.empty() || !project::isValidName(base) || (script && scripts::isBadName(base))) {
        MessageBoxA(static_cast<HWND>(scriptsPanel_), "El nombre no es valido.",
                    "Renombrar", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!scripts::renameNode(*project_, sel, base)) {
        MessageBoxA(static_cast<HWND>(scriptsPanel_),
                    "No se pudo renombrar.\n¿Ya existe un elemento con ese nombre?",
                    "Renombrar", MB_OK | MB_ICONERROR);
        return;
    }
    const std::string parent = parentSegment(sel);
    rebuildTree();
    selectRel(parent.empty() ? (script ? base + ".sk" : base)
                             : parent + "/" + (script ? base + ".sk" : base));
    updateProperties();
}

void CodeView::onDelete() {
    if (!project_) return;
    const std::string sel = selectedRel();
    if (sel.empty() || scripts::isProtected(sel)) return;

    const std::string message = "Borrar \"" + lastSegment(sel) + "\"?\n\n"
                                "Esta accion no se puede deshacer.";
    const int answer = MessageBoxA(static_cast<HWND>(scriptsPanel_), message.c_str(),
                                   "Borrar", MB_YESNO | MB_ICONWARNING);
    if (answer != IDYES) return;

    if (!scripts::removeNode(*project_, sel)) {
        MessageBoxA(static_cast<HWND>(scriptsPanel_), "No se pudo borrar el elemento.",
                    "Borrar", MB_OK | MB_ICONERROR);
        return;
    }
    const std::string parent = parentSegment(sel);
    rebuildTree();
    if (!parent.empty()) selectRel(parent);
    updateProperties();
}

long long __stdcall CodeView::wndProc(void* hwndPtr, unsigned int msg,
                                      unsigned long long wParam, long long lParam) {
    HWND hwnd = static_cast<HWND>(hwndPtr);
    CodeView* self =
        reinterpret_cast<CodeView*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        self = static_cast<CodeView*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, theme::backgroundBrush());

            char title[64]{};
            GetWindowTextA(hwnd, title, static_cast<int>(sizeof(title)));
            paintPanelHeader(hdc, rc, title);

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (ui::paintDarkButton(*dis)) return TRUE;
            break;
        }
        case WM_COMMAND: {
            if (HIWORD(wParam) != BN_CLICKED || !self) break;
            switch (LOWORD(wParam)) {
                case kIdNewScript: self->onNewScript(); return 0;
                case kIdNewFolder: self->onNewFolder(); return 0;
                case kIdRename: self->onRename(); return 0;
                case kIdDelete: self->onDelete(); return 0;
                default: break;
            }
            break;
        }
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lParam);
            if (!self || !self->tree_ || nm->hwndFrom != static_cast<HWND>(self->tree_)) break;
            if (nm->code == TVN_SELCHANGEDA) {
                self->updateProperties();
                self->updateButtons();
                return 0;
            }
            if (nm->code == TVN_DELETEITEMA) {
                auto* nmtv = reinterpret_cast<NMTREEVIEWA*>(lParam);
                delete reinterpret_cast<std::string*>(nmtv->itemOld.lParam);
                return 0;
            }
            break;
        }
        case WM_CTLCOLOREDIT: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, theme::listBackground());
            SetTextColor(hdc, theme::text());
            return reinterpret_cast<long long>(theme::listBackgroundBrush());
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            char text[64]{};
            GetWindowTextA(reinterpret_cast<HWND>(lParam), text, 64);
            const bool placeholder = std::string(text) == "Sin seleccion" || text[0] == '-';
            SetTextColor(hdc, placeholder ? theme::textDisabled() : theme::text());
            return reinterpret_cast<long long>(theme::backgroundBrush());
        }
        default:
            break;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

} // namespace sk
