#include "projects_panel.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>

#include <algorithm>
#include <ctime>
#include <filesystem>

#include "../core/config.h"
#include "../core/log.h"
#include "dark_button.h"
#include "prompt.h"
#include "theme.h"

namespace fs = std::filesystem;

namespace sk {

namespace {

const char* kPanelClass = "MotorSKProjectsPanel";

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
        wc.hbrBackground = theme::backgroundBrush();
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

    auto makeButton = [&](const char* text, int id) -> void* {
        HWND b = CreateWindowExA(0, "BUTTON", text,
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                 0, 0, 104, 32, static_cast<HWND>(hwnd_),
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 inst, nullptr);
        SendMessageA(b, WM_SETFONT, reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
        ui::makeDarkButton(b);
        return b;
    };

    btnNew_ = makeButton("Nuevo", kIdNew);
    btnRename_ = makeButton("Renombrar", kIdRename);
    btnDelete_ = makeButton("Borrar", kIdDelete);
    btnOpen_ = makeButton("Abrir", kIdOpen);

    // Sin WS_EX_CLIENTEDGE: el marco redondo lo pinta el panel en WM_PAINT.
    list_ = CreateWindowExA(0, "SysListView32", "",
                            WS_CHILD | WS_VISIBLE | LVS_REPORT |
                                LVS_SHOWSELALWAYS | LVS_SINGLESEL,
                            0, 0, 100, 100, static_cast<HWND>(hwnd_),
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdList)),
                            inst, nullptr);
    ListView_SetExtendedListViewStyle(static_cast<HWND>(list_),
                                      LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SendMessageA(static_cast<HWND>(list_), WM_SETFONT,
                 reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    ListView_SetBkColor(static_cast<HWND>(list_), theme::listBackground());
    ListView_SetTextBkColor(static_cast<HWND>(list_), theme::listBackground());
    ListView_SetTextColor(static_cast<HWND>(list_), theme::text());

    HWND header = ListView_GetHeader(static_cast<HWND>(list_));
    SendMessageA(header, WM_SETFONT, reinterpret_cast<WPARAM>(theme::uiHeaderFont()), TRUE);

    auto addColumn = [&](int index, const char* text, int width) {
        LVCOLUMNA col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<char*>(text);
        col.cx = width;
        ListView_InsertColumn(static_cast<HWND>(list_), index, &col);
    };
    addColumn(0, "Nombre", 220);
    addColumn(1, "Ultima apertura", 170);
    addColumn(2, "Ruta", 460);

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
    place(btnNew_, 12, 12, 104, 32);
    place(btnRename_, 126, 12, 104, 32);
    place(btnDelete_, 240, 12, 104, 32);
    place(btnOpen_, width - 116, 12, 104, 32);
    place(list_, 12, 60, width - 24, height - 72);
    applyListRegion();
}

void ProjectsPanel::applyListRegion() {
    if (!list_) return;
    HWND list = static_cast<HWND>(list_);
    RECT rc{};
    GetClientRect(list, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;
    // El sistema toma posesion de la region (la libera el mismo).
    HRGN region = CreateRoundRectRgn(0, 0, rc.right, rc.bottom, 15, 15);
    SetWindowRgn(list, region, TRUE);
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
    auto setEnabled = [](void* handle, bool enabled) {
        HWND hwnd = static_cast<HWND>(handle);
        if (!hwnd) return;
        if (!enabled) {
            // Un boton deshabilitado no puede quedar en estado hover.
            if (ui::DarkButton* b = ui::findDarkButton(hwnd); b && b->hover) {
                b->hover = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        EnableWindow(hwnd, enabled);
    };
    const bool hasSelection = selectedProject() != nullptr;
    setEnabled(btnRename_, hasSelection);
    setEnabled(btnDelete_, hasSelection);
    setEnabled(btnOpen_, hasSelection);
}

void ProjectsPanel::onSelectionChanged() {
    updateButtons();
}

void ProjectsPanel::openSelected() {
    const Project* p = selectedProject();
    if (p && onOpen_) onOpen_(p->folder);
}

void ProjectsPanel::newProject() {
    const std::string name =
        ui::promptText(static_cast<HWND>(hwnd_), "Nuevo proyecto", "");
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
                    "Ya existe un proyecto con ese nombre?",
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
        ui::promptText(static_cast<HWND>(hwnd_), "Renombrar proyecto", p->name.c_str());
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
                    "Ya existe otro proyecto con ese nombre?",
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
    const std::string message =
        "Borrar el proyecto \"" + p->name + "\"?\n\n"
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

// Pintado custom del ListView (filas) y de sus columnas (headers).
long long ProjectsPanel::customDraw(void* nmPtr) {
    auto* nm = static_cast<NMHDR*>(nmPtr);
    if (nm->hwndFrom == static_cast<HWND>(list_)) {
        auto* cd = reinterpret_cast<NMCUSTOMDRAW*>(nm);
        switch (cd->dwDrawStage) {
            case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;
            case CDDS_ITEMPREPAINT: {
                const int i = static_cast<int>(cd->dwItemSpec);
                const bool selected =
                    (ListView_GetItemState(static_cast<HWND>(list_), i, LVIS_SELECTED) &
                     LVIS_SELECTED) != 0;
                const COLORREF fill = selected ? theme::accent() : theme::listBackground();
                HBRUSH brush = CreateSolidBrush(fill);
                FillRect(cd->hdc, &cd->rc, brush);
                DeleteObject(brush);
                SetTextColor(cd->hdc, theme::text());
                SetBkColor(cd->hdc, fill);
                return CDRF_NEWFONT;
            }
            default:
                break;
        }
        return CDRF_DODEFAULT;
    }

    if (nm->hwndFrom == ListView_GetHeader(static_cast<HWND>(list_))) {
        auto* cd = reinterpret_cast<NMCUSTOMDRAW*>(nm);
        switch (cd->dwDrawStage) {
            case CDDS_PREPAINT: {
                // Fondo de todo el header, incluida la zona vacia a la
                // derecha de la ultima columna.
                HBRUSH brush = CreateSolidBrush(theme::headerBackground());
                FillRect(cd->hdc, &cd->rc, brush);
                DeleteObject(brush);
                return CDRF_NOTIFYITEMDRAW;
            }
            case CDDS_ITEMPREPAINT: {
                char headerText[128]{};
                HDITEMA item{};
                item.mask = HDI_TEXT;
                item.pszText = headerText;
                item.cchTextMax = static_cast<int>(sizeof(headerText));
                Header_GetItem(cd->hdr.hwndFrom, cd->dwItemSpec, &item);

                HBRUSH brush = CreateSolidBrush(theme::headerBackground());
                FillRect(cd->hdc, &cd->rc, brush);
                DeleteObject(brush);

                RECT rc = cd->rc;
                rc.left += 8;
                SetBkMode(cd->hdc, TRANSPARENT);
                SetTextColor(cd->hdc, theme::text());
                HFONT oldFont = static_cast<HFONT>(
                    SelectObject(cd->hdc, theme::uiHeaderFont()));
                DrawTextA(cd->hdc, headerText, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                SelectObject(cd->hdc, oldFont);
                return CDRF_SKIPDEFAULT;
            }
            default:
                break;
        }
        return CDRF_DODEFAULT;
    }

    return CDRF_DODEFAULT;
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
            case WM_PAINT: {
                // Fondo + marco redondo alrededor del ListView (el list
                // se pinta encima y solo queda visible la linea exterior).
                PAINTSTRUCT ps{};
                HDC hdc = BeginPaint(hwnd, &ps);
                FillRect(hdc, &ps.rcPaint, theme::backgroundBrush());
                if (self->list_) {
                    HWND list = static_cast<HWND>(self->list_);
                    RECT rc{};
                    GetWindowRect(list, &rc);
                    POINT topLeft{rc.left, rc.top};
                    POINT bottomRight{rc.right, rc.bottom};
                    ScreenToClient(hwnd, &topLeft);
                    ScreenToClient(hwnd, &bottomRight);
                    RECT frame{topLeft.x - 1, topLeft.y - 1,
                               bottomRight.x + 1, bottomRight.y + 1};
                    HPEN pen = CreatePen(PS_SOLID, 1, theme::border());
                    HBRUSH oldBrush = static_cast<HBRUSH>(
                        SelectObject(hdc, GetStockObject(NULL_BRUSH)));
                    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
                    RoundRect(hdc, frame.left, frame.top, frame.right, frame.bottom, 16, 16);
                    SelectObject(hdc, oldBrush);
                    SelectObject(hdc, oldPen);
                    DeleteObject(pen);
                }
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_DRAWITEM: {
                auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
                if (ui::paintDarkButton(*dis)) return TRUE;
                break;
            }
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
                if (nm->code == NM_CUSTOMDRAW) {
                    return self->customDraw(nm);
                }
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
