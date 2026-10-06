#include "place_view.h"

#include <windows.h>
#include <commctrl.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../core/log.h"
#include "dark_button.h"
#include "theme.h"
#include "workspace_tabs.h"

namespace sk {

namespace {

const char* kPanelClass = "MotorSKPlacePanel";

constexpr int kHeaderHeight = 36;

// Ids dentro del panel Properties (100.. = etiquetas, 200.. = valores).
constexpr int kIdStatus = 100;
constexpr int kIdAssocValue = 209;
constexpr int kIdAssocBtn = 210;

// Cabecera del panel + linea separadora, dentro del WM_PAINT propio.
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

HTREEITEM addTreeRoot(HWND tree, const char* text) {
    char label[64]{};
    lstrcpynA(label, text, 64);
    TVINSERTSTRUCTA ins{};
    ins.hParent = TVI_ROOT;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT;
    ins.item.pszText = label;
    return TreeView_InsertItem(tree, &ins);
}

// Escribe un campo Transform (ids 200..208) con formato compacto.
void setTransformField(HWND props, int id, float value) {
    HWND field = GetDlgItem(props, id);
    if (!field) return;
    char buf[32]{};
    snprintf(buf, sizeof(buf), "%.3g", value);
    SetWindowTextA(field, buf);
}

// Transform identico para raiz / sin seleccion: posicion 0, rotacion 0,
// escala 1.
void setTransformIdentity(HWND props) {
    for (int i = 0; i < 3; ++i) {
        setTransformField(props, 200 + i, 0.0f);
        setTransformField(props, 203 + i, 0.0f);
        setTransformField(props, 206 + i, 1.0f);
    }
}

HWND makeStatic(HWND parent, HINSTANCE inst, int id, const char* text,
                int x, int y, int w, int h, HFONT font) {
    HWND hwnd = CreateWindowExA(0, "STATIC", text,
                                WS_CHILD | WS_VISIBLE | SS_LEFT,
                                x, y, w, h, parent,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                inst, nullptr);
    SendMessageA(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return hwnd;
}

HWND makeEdit(HWND parent, HINSTANCE inst, int id, const char* text,
              int x, int y, int w, int h) {
    HWND hwnd = CreateWindowExA(0, "EDIT", text,
                                WS_CHILD | WS_VISIBLE | WS_BORDER |
                                ES_AUTOHSCROLL | ES_READONLY | WS_TABSTOP,
                                x, y, w, h, parent,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                inst, nullptr);
    SendMessageA(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    return hwnd;
}

} // namespace

bool PlaceView::create(void* parentHwnd) {
    parent_ = parentHwnd;

    static bool registered = false;
    if (!registered) {
        HINSTANCE inst = GetModuleHandleW(nullptr);

        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&PlaceView::wndProc);
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

    explorer_ = CreateWindowExA(0, kPanelClass, "Explorer",
                                WS_CHILD | WS_CLIPCHILDREN,
                                0, 0, kExplorerWidth, 100, parent, nullptr,
                                inst, this);
    properties_ = CreateWindowExA(0, kPanelClass, "Properties",
                                  WS_CHILD | WS_CLIPCHILDREN,
                                  0, 0, kPropertiesWidth, 100, parent, nullptr,
                                  inst, this);
    if (!explorer_ || !properties_) {
        SK_ERROR("PlaceView: CreateWindowEx fallo (error %lu)", GetLastError());
        return false;
    }

    // Explorer: arbol oscuro con las dos raices del spec.
    HWND tree = CreateWindowExA(0, WC_TREEVIEWA, "",
                                WS_CHILD | WS_VISIBLE |
                                TVS_HASBUTTONS | TVS_HASLINES |
                                TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                                6, kHeaderHeight + 6, kExplorerWidth - 12, 100,
                                static_cast<HWND>(explorer_),
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(1)),
                                inst, nullptr);
    tree_ = tree;
    SendMessageA(tree, WM_SETFONT, reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    TreeView_SetBkColor(tree, theme::listBackground());
    TreeView_SetTextColor(tree, theme::text());
    TreeView_SetLineColor(tree, theme::border());
    // Raiz unica por defecto: la dimension/escena del lugar.
    root_ = addTreeRoot(tree, "dimension01");

    // Properties: estado + bloque Transform (solo lectura hasta que haya
    // objetos que sincronizar con ModelScript).
    HWND hprop = static_cast<HWND>(properties_);
    makeStatic(hprop, inst, 100, "Sin objeto seleccionado",
               12, kHeaderHeight + 8, kPropertiesWidth - 24, 20,
               theme::uiFont());
    makeStatic(hprop, inst, 101, "Transform",
               12, kHeaderHeight + 34, 150, 22,
               theme::uiHeaderFont());

    const char* rowLabels[3] = {"Position", "Rotation", "Scale"};
    const char* rowValues[3] = {"0", "0", "1"};
    for (int row = 0; row < 3; ++row) {
        const int y = kHeaderHeight + 62 + row * 34;
        makeStatic(hprop, inst, 102 + row, rowLabels[row],
                   12, y + 2, 70, 22, theme::uiFont());
        for (int axis = 0; axis < 3; ++axis) {
            makeEdit(hprop, inst, 200 + row * 3 + axis, rowValues[row],
                     84 + axis * 68, y, 64, 24);
        }
    }

    // ModelScript: script asociado al objeto seleccionado.
    const int assocY = kHeaderHeight + 62 + 3 * 34;
    makeStatic(hprop, inst, 105, "Asociado a",
               12, assocY + 2, 70, 22, theme::uiFont());
    makeStatic(hprop, inst, kIdAssocValue, "-",
               84, assocY + 2, 126, 22, theme::uiFont());
    HWND assocBtn = CreateWindowExA(0, "BUTTON", "Cambiar...",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                        BS_OWNERDRAW,
                                    214, assocY, 74, 26, hprop,
                                    reinterpret_cast<HMENU>(
                                        static_cast<INT_PTR>(kIdAssocBtn)),
                                    inst, nullptr);
    SendMessageA(assocBtn, WM_SETFONT,
                 reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    ui::makeDarkButton(assocBtn);
    EnableWindow(assocBtn, FALSE); // sin objeto seleccionado aun

    resize(rc.right, rc.bottom);
    SK_INFO("Paneles PLACE listos (Properties %d izq + Explorer %d der)",
            kPropertiesWidth, kExplorerWidth);
    return true;
}

void PlaceView::destroy() {
    if (explorer_) {
        DestroyWindow(static_cast<HWND>(explorer_));
        explorer_ = nullptr;
    }
    if (properties_) {
        DestroyWindow(static_cast<HWND>(properties_));
        properties_ = nullptr;
    }
    tree_ = nullptr;
    root_ = nullptr;
}

void PlaceView::setVisible(bool visible) {
    visible_ = visible;
    if (explorer_) ShowWindow(static_cast<HWND>(explorer_), visible ? SW_SHOW : SW_HIDE);
    if (properties_) ShowWindow(static_cast<HWND>(properties_), visible ? SW_SHOW : SW_HIDE);
}

void PlaceView::resize(int width, int height) {
    layoutPanels(width, height);
}

void PlaceView::addObject(const std::string& name) {
    if (!tree_ || !root_) return;
    HWND tree = static_cast<HWND>(tree_);

    char label[64]{};
    lstrcpynA(label, name.c_str(), 64);
    TVINSERTSTRUCTA ins{};
    ins.hParent = static_cast<HTREEITEM>(root_);
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT;
    ins.item.pszText = label;
    HTREEITEM item = TreeView_InsertItem(tree, &ins);

    TreeView_Expand(tree, static_cast<HTREEITEM>(root_), TVE_EXPAND);
    if (item) TreeView_Select(tree, item, TVGN_CARET);
}

void PlaceView::clearObjects() {
    if (!tree_ || !root_) return;
    HWND tree = static_cast<HWND>(tree_);

    HTREEITEM child = TreeView_GetChild(tree, static_cast<HTREEITEM>(root_));
    while (child) {
        HTREEITEM next = TreeView_GetNextSibling(tree, child);
        TreeView_DeleteItem(tree, child);
        child = next;
    }
    TreeView_Select(tree, static_cast<HTREEITEM>(root_), TVGN_CARET);
}

void PlaceView::showObject(const SceneObject& object) {
    HWND props = static_cast<HWND>(properties_);
    if (!props) return;
    selectedName_ = object.name;
    if (HWND status = GetDlgItem(props, kIdStatus)) {
        SetWindowTextA(status, object.name.c_str());
    }
    for (int axis = 0; axis < 3; ++axis) {
        setTransformField(props, 200 + axis, (&object.position.x)[axis]);
        setTransformField(props, 203 + axis, (&object.rotation.x)[axis]);
        setTransformField(props, 206 + axis, (&object.scale.x)[axis]);
    }
    if (HWND assoc = GetDlgItem(props, kIdAssocValue)) {
        SetWindowTextA(assoc, object.script.empty() ? "-" : object.script.c_str());
    }
    if (HWND btn = GetDlgItem(props, kIdAssocBtn)) EnableWindow(btn, TRUE);
}

void PlaceView::showRoot() {
    HWND props = static_cast<HWND>(properties_);
    if (!props) return;
    selectedName_.clear();
    if (HWND status = GetDlgItem(props, kIdStatus)) {
        SetWindowTextA(status, "dimension01");
    }
    setTransformIdentity(props);
    if (HWND assoc = GetDlgItem(props, kIdAssocValue)) SetWindowTextA(assoc, "-");
    if (HWND btn = GetDlgItem(props, kIdAssocBtn)) EnableWindow(btn, FALSE);
}

void PlaceView::showNoSelection() {
    HWND props = static_cast<HWND>(properties_);
    if (!props) return;
    selectedName_.clear();
    if (HWND status = GetDlgItem(props, kIdStatus)) {
        SetWindowTextA(status, "Sin objeto seleccionado");
    }
    setTransformIdentity(props);
    if (HWND assoc = GetDlgItem(props, kIdAssocValue)) SetWindowTextA(assoc, "-");
    if (HWND btn = GetDlgItem(props, kIdAssocBtn)) EnableWindow(btn, FALSE);
}

void PlaceView::showMultiple(int count) {
    HWND props = static_cast<HWND>(properties_);
    if (!props) return;
    selectedName_.clear();
    char text[64]{};
    snprintf(text, sizeof(text), "-%d objetos seleccionados-", count);
    if (HWND status = GetDlgItem(props, kIdStatus)) SetWindowTextA(status, text);
    for (int row = 0; row < 3; ++row) {
        for (int axis = 0; axis < 3; ++axis) {
            if (HWND field = GetDlgItem(props, 200 + row * 3 + axis)) {
                SetWindowTextA(field, "-");
            }
        }
    }
    if (HWND assoc = GetDlgItem(props, kIdAssocValue)) SetWindowTextA(assoc, "-");
    if (HWND btn = GetDlgItem(props, kIdAssocBtn)) EnableWindow(btn, FALSE);
}

void PlaceView::syncTreeSelection(const std::string& primary) {
    if (!tree_) return;
    if (primary.empty()) {
        suppressNotify_ = true;
        TreeView_Select(static_cast<HWND>(tree_), nullptr, TVGN_CARET);
        suppressNotify_ = false;
        return;
    }
    HWND tree = static_cast<HWND>(tree_);
    HTREEITEM child = TreeView_GetChild(tree, static_cast<HTREEITEM>(root_));
    while (child) {
        char text[64]{};
        TVITEMA tvi{};
        tvi.hItem = child;
        tvi.mask = TVIF_TEXT;
        tvi.pszText = text;
        tvi.cchTextMax = static_cast<int>(sizeof(text));
        TreeView_GetItem(tree, &tvi);
        if (primary == text) {
            suppressNotify_ = true;
            TreeView_Select(tree, child, TVGN_CARET);
            suppressNotify_ = false;
            return;
        }
        child = TreeView_GetNextSibling(tree, child);
    }
}

void PlaceView::notifySelection() {
    if (suppressNotify_) return;
    if (!onSelectionChanged_ || !tree_) return;
    HWND tree = static_cast<HWND>(tree_);
    HTREEITEM item = TreeView_GetSelection(tree);
    if (!item) {
        onSelectionChanged_(std::string());
        return;
    }
    char text[64]{};
    TVITEMA tvi{};
    tvi.hItem = item;
    tvi.mask = TVIF_TEXT;
    tvi.pszText = text;
    tvi.cchTextMax = static_cast<int>(sizeof(text));
    TreeView_GetItem(tree, &tvi);
    onSelectionChanged_(text);
}

void PlaceView::layoutPanels(int width, int height) {
    if (!explorer_ || !properties_) return;
    const int y = WorkspaceTabs::kTopBandHeight;
    int panelH = height - y;
    if (panelH < 0) panelH = 0;

    // Properties a la izquierda, Explorer a la derecha.
    MoveWindow(static_cast<HWND>(properties_), 0, y, kPropertiesWidth, panelH, TRUE);
    MoveWindow(static_cast<HWND>(explorer_),
               width - kExplorerWidth, y, kExplorerWidth, panelH, TRUE);

    if (tree_) {
        MoveWindow(static_cast<HWND>(tree_), 6, kHeaderHeight + 6,
                   kExplorerWidth - 12, panelH - kHeaderHeight - 12, TRUE);
    }
}

long long __stdcall PlaceView::wndProc(void* hwndPtr, unsigned int msg,
                                        unsigned long long wParam,
                                        long long lParam) {
    HWND hwnd = static_cast<HWND>(hwndPtr);
    PlaceView* self =
        reinterpret_cast<PlaceView*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        self = static_cast<PlaceView*>(cs->lpCreateParams);
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
            if (!self) break;
            if (LOWORD(wParam) == kIdAssocBtn && HIWORD(wParam) == BN_CLICKED) {
                if (!self->selectedName_.empty() && self->onAssocEdit_) {
                    self->onAssocEdit_(self->selectedName_);
                }
                return 0;
            }
            break;
        }
        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<NMHDR*>(lParam);
            if (self && self->tree_ &&
                hdr->hwndFrom == static_cast<HWND>(self->tree_) &&
                (hdr->code == TVN_SELCHANGEDA || hdr->code == TVN_SELCHANGEDW)) {
                self->notifySelection();
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
            // El estado "Sin objeto seleccionado" (y los "-" de valores
            // vacios) van apagados; el resto (y las etiquetas Transform)
            // en color normal de texto.
            char text[64]{};
            GetWindowTextA(reinterpret_cast<HWND>(lParam), text, 64);
            const bool muted = std::strcmp(text, "Sin objeto seleccionado") == 0 ||
                               text[0] == '-';
            SetTextColor(hdc, muted ? theme::textDisabled() : theme::text());
            return reinterpret_cast<long long>(theme::backgroundBrush());
        }
        default:
            break;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

} // namespace sk
