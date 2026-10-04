#include "projects_panel.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#include <algorithm>
#include <ctime>
#include <filesystem>

#include "../core/config.h"
#include "../core/log.h"

namespace fs = std::filesystem;

namespace sk {

namespace {

const char* kPanelClass = "MotorSKProjectsPanel";
const char* kPromptClass = "MotorSKPrompt";

enum ControlId {
    kIdList = 1001,
    kIdNew = 1002,
    kIdRename = 1003,
    kIdDelete = 1004,
    kIdOpen = 1005,
};

std::string formatEpoch(long long t) {
    if (t <= 0) return "";
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    localtime_s(&tm, &tt);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M", &tm);
    return buf;
}

// ---------------------------------------------------------------------------
// Dialogo modal de texto (Win32 no tiene InputBox).
// Devuelve el texto aceptado o "" si se cancelo.
// ---------------------------------------------------------------------------

struct PromptState {
    HWND edit = nullptr;
    std::string value;
    bool accepted = false;
    bool done = false;
};

PromptState* gPrompt = nullptr;

long long __stdcall promptWndProc(void* hwndPtr, unsigned int msg,
                                  unsigned long long wParam, long long lParam) {
    HWND hwnd = static_cast<HWND>(hwndPtr);
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE inst = GetModuleHandleW(nullptr);
            HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

            HWND label = CreateWindowExA(0, "STATIC", "Nombre:",
                                         WS_CHILD | WS_VISIBLE,
                                         12, 12, 316, 18, hwnd, nullptr, inst, nullptr);
            HWND edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                        12, 34, 316, 24, hwnd, nullptr, inst, nullptr);
            HWND ok = CreateWindowExA(0, "BUTTON", "Aceptar",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                      158, 72, 82, 26, hwnd,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                                      inst, nullptr);
            HWND cancel = CreateWindowExA(0, "BUTTON", "Cancelar",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                          246, 72, 82, 26, hwnd,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                                          inst, nullptr);

            for (HWND c : {label, edit, ok, cancel}) {
                SendMessageA(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }
            if (gPrompt) gPrompt->edit = edit;
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == IDOK && gPrompt) {
                char buf[512]{};
                GetWindowTextA(gPrompt->edit, buf, static_cast<int>(sizeof(buf)));
                gPrompt->value = buf;
                gPrompt->accepted = true;
                DestroyWindow(hwnd);
            } else if (id == IDCANCEL) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (gPrompt) gPrompt->done = true;
            return 0;
        default:
            break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

std::string promptText(HWND owner, const char* title, const char* initial) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&promptWndProc);
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOW + 1));
        wc.lpszClassName = kPromptClass;
        RegisterClassExA(&wc);
        registered = true;
    }

    PromptState state;
    gPrompt = &state;

    RECT rect{0, 0, 344, 112};
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&rect, style, FALSE);

    // Centrar sobre la ventana padre.
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - (rect.right - rect.left)) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - (rect.bottom - rect.top)) / 2;

    HWND hwnd = CreateWindowExA(WS_EX_TOOLWINDOW, kPromptClass, title, style | WS_VISIBLE,
                                x, y, rect.right - rect.left, rect.bottom - rect.top,
                                owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) {
        gPrompt = nullptr;
        return "";
    }

    SetWindowTextA(state.edit, initial);
    SetFocus(state.edit);
    SendMessageA(state.edit, EM_SETSEL, 0, -1);

    EnableWindow(owner, FALSE);

    MSG msg{};
    while (!state.done && GetMessageA(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageA(hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    gPrompt = nullptr;
    return state.accepted ? state.value : "";
}

} // namespace

// ---------------------------------------------------------------------------
// ProjectsPanel
// ---------------------------------------------------------------------------

bool ProjectsPanel::create(void* parentHwnd, Config* config, OpenHandler onOpen) {
    parent_ = parentHwnd;
    config_ = config;
    onOpen_ = std::move(onOpen);

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&ProjectsPanel::wndProc);
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOW + 1));
        wc.lpszClassName = kPanelClass;
        RegisterClassExA(&wc);
        registered = true;
    }

    HWND parent = static_cast<HWND>(parent_);
    RECT rc{};
    GetClientRect(parent, &rc);

    hwnd_ = CreateWindowExA(0, kPanelClass, "", WS_CHILD | WS_VISIBLE,
                            0, 0, rc.right, rc.bottom, parent, nullptr,
                            GetModuleHandleW(nullptr), this);
    if (!hwnd_) {
        SK_ERROR("ProjectsPanel: CreateWindowEx fallo (error %lu)", GetLastError());
        return false;
    }

    HINSTANCE inst = GetModuleHandleW(nullptr);
    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    auto makeButton = [&](const char* text, int id) -> void* {
        HWND b = CreateWindowExA(0, "BUTTON", text,
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                 0, 0, 92, 26, static_cast<HWND>(hwnd_),
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 inst, nullptr);
        SendMessageA(b, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return b;
    };

    btnNew_ = makeButton("Nuevo", kIdNew);
    btnRename_ = makeButton("Renombrar", kIdRename);
    btnDelete_ = makeButton("Borrar", kIdDelete);
    btnOpen_ = makeButton("Abrir", kIdOpen);

    list_ = CreateWindowExA(WS_EX_CLIENTEDGE, "SysListView32", "",
                            WS_CHILD | WS_VISIBLE | LVS_REPORT |
                                LVS_SHOWSELALWAYS | LVS_SINGLESEL,
                            0, 0, 100, 100, static_cast<HWND>(hwnd_),
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdList)),
                            inst, nullptr);
    ListView_SetExtendedListViewStyle(static_cast<HWND>(list_),
                                      LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SendMessageA(static_cast<HWND>(list_), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    auto addColumn = [&](int index, const char* text, int width) {
        LVCOLUMNA col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<char*>(text);
        col.cx = width;
        ListView_InsertColumn(static_cast<HWND>(list_), index, &col);
    };
    addColumn(0, "Nombre", 200);
    addColumn(1, "Ultima apertura", 140);
    addColumn(2, "Ruta", 420);

    resize(rc.right, rc.bottom);
    refresh();
    updateButtons();

    SK_INFO("Panel de proyectos listo (raiz: %s)", project::rootFolder().c_str());
    return true;
}

void ProjectsPanel::destroy() {
    if (hwnd_) {
        DestroyWindow(static_cast<HWND>(hwnd_));
        hwnd_ = nullptr;
        list_ = nullptr;
    }
}

void ProjectsPanel::setVisible(bool visible) {
    visible_ = visible;
    if (hwnd_) ShowWindow(static_cast<HWND>(hwnd_), visible ? SW_SHOW : SW_HIDE);
}

void ProjectsPanel::resize(int width, int height) {
    if (!hwnd_) return;
    MoveWindow(static_cast<HWND>(hwnd_), 0, 0, width, height, TRUE);

    auto place = [](void* handle, int x, int y, int w, int h) {
        if (handle) MoveWindow(static_cast<HWND>(handle), x, y, w, h, TRUE);
    };
    place(btnNew_, 8, 8, 92, 26);
    place(btnRename_, 106, 8, 92, 26);
    place(btnDelete_, 204, 8, 92, 26);
    place(btnOpen_, width - 100, 8, 92, 26);
    place(list_, 8, 44, width - 16, height - 52);
}

void ProjectsPanel::refresh() {
    projects_ = project::scan();

    // Los recientes cuya carpeta ya no existe se limpian del config.
    if (config_) {
        bool changed = false;
        const std::vector<Config::Recent> recents = config_->recents();
        for (const Config::Recent& r : recents) {
            std::error_code ec;
            if (!fs::exists(r.folder, ec)) {
                config_->removeRecent(r.folder);
                changed = true;
            }
        }
        if (changed) config_->save();
    }

    auto epochOf = [this](const Project& p) -> long long {
        if (config_) {
            for (const Config::Recent& r : config_->recents()) {
                if (r.folder == p.folder) return r.openedAt;
            }
        }
        return 0;
    };

    // Recientes primero (mas reciente arriba), el resto por nombre.
    std::stable_sort(projects_.begin(), projects_.end(),
                     [&](const Project& a, const Project& b) {
                         const long long ea = epochOf(a);
                         const long long eb = epochOf(b);
                         if ((ea > 0) != (eb > 0)) return ea > 0;
                         if (ea > 0 && eb > 0 && ea != eb) return ea > eb;
                         return a.name < b.name;
                     });

    if (!list_) return;

    ListView_DeleteAllItems(static_cast<HWND>(list_));
    for (int i = 0; i < static_cast<int>(projects_.size()); ++i) {
        const Project& p = projects_[i];

        LVITEMA item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = i;
        item.pszText = const_cast<char*>(p.name.c_str());
        item.lParam = i;
        ListView_InsertItem(static_cast<HWND>(list_), &item);

        const std::string opened = formatEpoch(epochOf(p));
        ListView_SetItemText(static_cast<HWND>(list_), i, 1,
                             const_cast<char*>(opened.c_str()));
        ListView_SetItemText(static_cast<HWND>(list_), i, 2,
                             const_cast<char*>(p.folder.c_str()));
    }

    updateButtons();
    SK_INFO("Proyectos encontrados: %d", static_cast<int>(projects_.size()));
}

int ProjectsPanel::selectedIndex() const {
    if (!list_) return -1;
    return ListView_GetNextItem(static_cast<HWND>(list_), -1, LVNI_SELECTED);
}

const Project* ProjectsPanel::selectedProject() const {
    const int i = selectedIndex();
    if (i < 0 || i >= static_cast<int>(projects_.size())) return nullptr;
    return &projects_[i];
}

void ProjectsPanel::updateButtons() {
    const bool hasSelection = selectedProject() != nullptr;
    if (btnRename_) EnableWindow(static_cast<HWND>(btnRename_), hasSelection);
    if (btnDelete_) EnableWindow(static_cast<HWND>(btnDelete_), hasSelection);
    if (btnOpen_) EnableWindow(static_cast<HWND>(btnOpen_), hasSelection);
}

void ProjectsPanel::onSelectionChanged() {
    updateButtons();
}

void ProjectsPanel::openSelected() {
    const Project* p = selectedProject();
    if (p && onOpen_) onOpen_(p->folder);
}

namespace {

// Selecciona en la lista el proyecto con esta carpeta (tras crear/renombrar).
void selectFolder(HWND list, const std::vector<Project>& projects,
                  const std::string& folder) {
    for (int i = 0; i < static_cast<int>(projects.size()); ++i) {
        if (projects[i].folder == folder) {
            ListView_SetItemState(list, i, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(list, i, FALSE);
            return;
        }
    }
}

} // namespace

void ProjectsPanel::newProject() {
    const std::string name =
        promptText(static_cast<HWND>(hwnd_), "Nuevo proyecto", "");
    if (name.empty()) return;

    if (!project::isValidName(name)) {
        MessageBoxA(static_cast<HWND>(hwnd_),
                    "Nombre no valido: no puede contener \\ / : * ? \" < > |",
                    "Nuevo proyecto", MB_OK | MB_ICONWARNING);
        return;
    }

    std::string folder;
    if (!project::create(name, folder)) {
        MessageBoxA(static_cast<HWND>(hwnd_),
                    "No se pudo crear la carpeta del proyecto.\n"
                    "¿Ya existe un proyecto con ese nombre?",
                    "Nuevo proyecto", MB_OK | MB_ICONERROR);
        return;
    }

    refresh();
    selectFolder(static_cast<HWND>(list_), projects_, folder);
    updateButtons();
}

void ProjectsPanel::renameSelected() {
    const Project* p = selectedProject();
    if (!p) return;

    const std::string folder = p->folder;
    const std::string name =
        promptText(static_cast<HWND>(hwnd_), "Renombrar proyecto", p->name.c_str());
    if (name.empty()) return;

    if (!project::isValidName(name)) {
        MessageBoxA(static_cast<HWND>(hwnd_),
                    "Nombre no valido: no puede contener \\ / : * ? \" < > |",
                    "Renombrar proyecto", MB_OK | MB_ICONWARNING);
        return;
    }

    std::string newFolder;
    if (!project::rename(folder, name, newFolder)) {
        MessageBoxA(static_cast<HWND>(hwnd_),
                    "No se pudo renombrar el proyecto.\n"
                    "¿Ya existe otro proyecto con ese nombre?",
                    "Renombrar proyecto", MB_OK | MB_ICONERROR);
        return;
    }

    if (config_ && newFolder != folder) {
        // La carpeta cambio: el reciente debe apuntar a la nueva ruta.
        config_->removeRecent(folder);
        config_->save();
    }

    refresh();
    selectFolder(static_cast<HWND>(list_), projects_, newFolder);
    updateButtons();
}

void ProjectsPanel::deleteSelected() {
    const Project* p = selectedProject();
    if (!p) return;

    const std::string folder = p->folder;
    std::string message =
        "¿Borrar el proyecto \"" + p->name + "\"?\n\n" +
        "Se eliminara la carpeta completa:\n" + folder +
        "\n\nEsta accion no se puede deshacer.";

    if (MessageBoxA(static_cast<HWND>(hwnd_), message.c_str(), "Borrar proyecto",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        return;
    }

    if (project::remove(folder)) {
        if (config_) {
            config_->removeRecent(folder);
            config_->save();
        }
        refresh();
    } else {
        MessageBoxA(static_cast<HWND>(hwnd_), "No se pudo borrar la carpeta.",
                    "Borrar proyecto", MB_OK | MB_ICONERROR);
    }
}

long long __stdcall ProjectsPanel::wndProc(void* hwndPtr, unsigned int msg,
                                            unsigned long long wParam,
                                            long long lParam) {
    HWND hwnd = static_cast<HWND>(hwndPtr);
    ProjectsPanel* self =
        reinterpret_cast<ProjectsPanel*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        self = static_cast<ProjectsPanel*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self) {
        switch (msg) {
            case WM_COMMAND: {
                const int id = LOWORD(wParam);
                if (id == kIdNew) self->newProject();
                else if (id == kIdRename) self->renameSelected();
                else if (id == kIdDelete) self->deleteSelected();
                else if (id == kIdOpen) self->openSelected();
                return 0;
            }
            case WM_NOTIFY: {
                auto* nm = reinterpret_cast<NMHDR*>(lParam);
                if (nm->idFrom == kIdList) {
                    if (nm->code == LVN_ITEMCHANGED) {
                        self->onSelectionChanged();
                    } else if (nm->code == NM_DBLCLK) {
                        self->openSelected();
                    } else if (nm->code == LVN_KEYDOWN) {
                        auto* key = reinterpret_cast<NMLVKEYDOWN*>(lParam);
                        if (key->wVKey == VK_RETURN) self->openSelected();
                    }
                }
                return 0;
            }
            default:
                break;
        }
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

} // namespace sk
