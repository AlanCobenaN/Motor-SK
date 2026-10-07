#include "place_view.h"

#include <windows.h>
#include <commctrl.h>

#include <cstdio>
#include <cstdlib>
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

// Raiz unica del arbol Explorer: la dimension/escena del lugar. Tambien
// es el "Parent" que muestra Properties para cualquier parte.
constexpr const char* kRootName = "dimension01";

// Ids dentro del panel Properties (100.. = etiquetas, 200.. = valores,
// 300.. = resumenes "x, y, z" de cada fila del Transform).
constexpr int kIdStatus = 100;
constexpr int kIdTransform = 101;
constexpr int kIdRowLabel = 102;   // 102..104: Position / Rotation / Scale
constexpr int kIdParentLabel = 105;
constexpr int kIdNameLabel = 106;
constexpr int kIdAxisLabel = 110;  // 110..118: X / Y / Z
constexpr int kIdParentValue = 209;
constexpr int kIdSummary = 300;    // 300..302

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
// keep (opcional) guarda el texto escrito para poder restaurarlo si el
// usuario escribe algo que no es un numero.
void setTransformField(HWND props, int id, float value,
                       std::string* keep = nullptr) {
    HWND field = GetDlgItem(props, id);
    if (!field) return;
    char buf[32]{};
    snprintf(buf, sizeof(buf), "%.3g", value);
    if (keep) *keep = buf;
    SetWindowTextA(field, buf);
}

// Transform identico para raiz / sin seleccion: posicion 0, rotacion 0,
// escala 1. keep apunta a los 9 textos guardados (nullptr para saltar).
void setTransformIdentity(HWND props, std::string* keep) {
    for (int i = 0; i < 3; ++i) {
        setTransformField(props, 200 + i, 0.0f, keep ? &keep[i] : nullptr);
        setTransformField(props, 203 + i, 0.0f,
                          keep ? &keep[3 + i] : nullptr);
        setTransformField(props, 206 + i, 1.0f,
                          keep ? &keep[6 + i] : nullptr);
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

// Enter en un campo: aplica el valor al momento (mismo camino que
// EN_KILLFOCUS) y se traga el "ding" del EDIT, que no ve el \r.
LRESULT CALLBACK fieldSubclassProc(HWND hwnd, UINT msg, WPARAM wParam,
                                   LPARAM lParam, UINT_PTR uId,
                                   DWORD_PTR dwRef) {
    (void)uId;
    (void)dwRef;
    if (msg == WM_CHAR && wParam == '\r') {
        SendMessageA(GetParent(hwnd), WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(hwnd), EN_KILLFOCUS),
                     reinterpret_cast<LPARAM>(hwnd));
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

HWND makeEdit(HWND parent, HINSTANCE inst, int id, const char* text,
              int x, int y, int w, int h) {
    HWND hwnd = CreateWindowExA(0, "EDIT", text,
                                WS_CHILD | WS_VISIBLE | WS_BORDER |
                                ES_AUTOHSCROLL | WS_TABSTOP,
                                x, y, w, h, parent,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                inst, nullptr);
    SendMessageA(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    SetWindowSubclass(hwnd, fieldSubclassProc, 1, 0);
    return hwnd;
}

// Triangulito de cascada dentro del WM_PAINT del panel: derecha si esta
// plegada, abajo si esta desplegada. rect es la zona de click (amplia);
// el triangulo dibujado es un 25% mas grande que la version anterior
// (redondeado a pixeles enteros) para que se vea sin dominar la fila.
void drawTriangle(HDC hdc, const int rect[4], bool open) {
    if (rect[2] <= rect[0] || rect[3] <= rect[1]) return;
    const int cx = (rect[0] + rect[2]) / 2;
    const int cy = (rect[1] + rect[3]) / 2;
    int hw = ((rect[2] - rect[0] - 6) * 5 + 16) / 32;
    int hh = ((rect[3] - rect[1] - 8) * 5 + 16) / 32;
    if (hw < 1) hw = 1;
    if (hh < 1) hh = 1;
    const int l = cx - hw, r = cx + hw;
    const int t = cy - hh, b = cy + hh;
    POINT poly[3];
    if (open) {
        poly[0] = POINT{l, t};
        poly[1] = POINT{r, t};
        poly[2] = POINT{(l + r) / 2, b};
    } else {
        poly[0] = POINT{l, t};
        poly[1] = POINT{l, b};
        poly[2] = POINT{r, (t + b) / 2};
    }
    HBRUSH brush = CreateSolidBrush(theme::text());
    HPEN pen = CreatePen(PS_SOLID, 1, theme::text());
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, brush));
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    Polygon(hdc, poly, 3);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    DeleteObject(brush);
}

bool inTri(const int rect[4], int x, int y) {
    return x >= rect[0] && x < rect[2] && y >= rect[1] && y < rect[3];
}

// Resumen "x, y, z" de una fila a partir de los tres textos guardados
// (los ejes ya estan formateados con %.3g).
std::string joinSummary(const std::string* fields) {
    if (fields[0] == "-") return "-";
    return fields[0] + ", " + fields[1] + ", " + fields[2];
}

// Parsea un resumen "x, y, z": coma como separador y punto como
// decimal (el teclado espanol con coma decimal solo vale en los ejes
// sueltos, donde strtof los acepta tras la sustitucion).
bool parseTriple(const char* text, float out[3]) {
    const char* p = text;
    for (int i = 0; i < 3; ++i) {
        char* end = nullptr;
        out[i] = strtof(p, &end);
        if (end == p) return false;
        p = end;
        while (*p == ' ') ++p;
        if (i < 2) {
            if (*p != ',') return false;
            ++p;
        }
    }
    return *p == '\0';
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
    root_ = addTreeRoot(tree, kRootName);

    // Properties: Nombre + Transform (cascada Position/Rotation/Scale,
    // cada fila con su resumen "x, y, z" editable al lado y los tres
    // ejes al desplegarla) y al final la fila Parent (solo lectura).
    // Transform arranca abierto y las filas plegadas; layoutProperties
    // coloca y muestra/oculta segun el estado de las cascadas.
    HWND hprop = static_cast<HWND>(properties_);
    makeStatic(hprop, inst, kIdNameLabel, "Nombre",
               12, kHeaderHeight + 8, 70, 20, theme::uiFont());
    makeStatic(hprop, inst, kIdStatus, "Sin objeto seleccionado",
               84, kHeaderHeight + 8, kPropertiesWidth - 96, 20,
               theme::uiFont());
    makeStatic(hprop, inst, kIdTransform, "Transform",
               34, 0, 200, 22, theme::uiHeaderFont());

    const char* rowLabels[3] = {"Position", "Rotation", "Scale"};
    const char* rowValues[3] = {"0", "0", "1"};
    const char* axisNames[3] = {"X", "Y", "Z"};
    for (int row = 0; row < 3; ++row) {
        makeStatic(hprop, inst, kIdRowLabel + row, rowLabels[row],
                   54, 0, 64, 20, theme::uiFont());
        char summary[32]{};
        snprintf(summary, sizeof(summary), "%s, %s, %s",
                 rowValues[row], rowValues[row], rowValues[row]);
        makeEdit(hprop, inst, kIdSummary + row, summary, 122, 0, 166, 22);
        summaryText_[row] = summary;
        for (int axis = 0; axis < 3; ++axis) {
            makeStatic(hprop, inst, kIdAxisLabel + row * 3 + axis,
                       axisNames[axis], 64, 0, 14, 20, theme::uiFont());
            makeEdit(hprop, inst, 200 + row * 3 + axis, rowValues[row],
                     84, 0, 150, 22);
            fieldText_[row * 3 + axis] = rowValues[row];
        }
    }

    // Parent del objeto: la raiz del arbol. Solo lectura (de momento);
    // el valor es un STATIC y va apagado en el pintado del panel.
    makeStatic(hprop, inst, kIdParentLabel, "Parent",
               12, 0, 70, 20, theme::uiFont());
    makeStatic(hprop, inst, kIdParentValue, "-",
               84, 0, kPropertiesWidth - 96, 20, theme::uiFont());
    layoutProperties();

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
        setTransformField(props, 200 + axis, (&object.position.x)[axis],
                          &fieldText_[axis]);
        setTransformField(props, 203 + axis, (&object.rotation.x)[axis],
                          &fieldText_[3 + axis]);
        setTransformField(props, 206 + axis, (&object.scale.x)[axis],
                          &fieldText_[6 + axis]);
    }
    updateSummaries();
    if (HWND parent = GetDlgItem(props, kIdParentValue)) {
        SetWindowTextA(parent, kRootName);
    }
}

void PlaceView::showRoot() {
    HWND props = static_cast<HWND>(properties_);
    if (!props) return;
    selectedName_.clear();
    if (HWND status = GetDlgItem(props, kIdStatus)) {
        SetWindowTextA(status, kRootName);
    }
    setTransformIdentity(props, fieldText_);
    updateSummaries();
    if (HWND parent = GetDlgItem(props, kIdParentValue)) {
        SetWindowTextA(parent, "-"); // la raiz no tiene padre
    }
}

void PlaceView::showNoSelection() {
    HWND props = static_cast<HWND>(properties_);
    if (!props) return;
    selectedName_.clear();
    if (HWND status = GetDlgItem(props, kIdStatus)) {
        SetWindowTextA(status, "Sin objeto seleccionado");
    }
    setTransformIdentity(props, fieldText_);
    updateSummaries();
    if (HWND parent = GetDlgItem(props, kIdParentValue)) SetWindowTextA(parent, "-");
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
            fieldText_[row * 3 + axis] = "-";
        }
    }
    updateSummaries();
    // Todos los partes cuelgan de la raiz de momento.
    if (HWND parent = GetDlgItem(props, kIdParentValue)) {
        SetWindowTextA(parent, kRootName);
    }
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

// Reescribe los resumenes "x, y, z" (ids 300..302) a partir de los
// textos de los ejes ya guardados en fieldText_.
void PlaceView::updateSummaries() {
    HWND props = static_cast<HWND>(properties_);
    if (!props) return;
    for (int row = 0; row < 3; ++row) {
        summaryText_[row] = joinSummary(&fieldText_[row * 3]);
        if (HWND sum = GetDlgItem(props, kIdSummary + row)) {
            SetWindowTextA(sum, summaryText_[row].c_str());
        }
    }
}

// Coloca los controles del panel Properties segun el estado de las
// cascadas (Transform y cada fila) y muestra/oculta lo que toque. Las
// filas plegadas siguen mostrando su resumen "x, y, z" editable.
void PlaceView::layoutProperties() {
    HWND props = static_cast<HWND>(properties_);
    if (!props) return;
    auto place = [props](int id, int x, int y, int w, int h, bool show) {
        HWND ctl = GetDlgItem(props, id);
        if (!ctl) return;
        SetWindowPos(ctl, nullptr, x, y, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(ctl, show ? SW_SHOW : SW_HIDE);
    };

    int y = kHeaderHeight + 8;
    place(kIdNameLabel, 12, y, 70, 20, true);
    place(kIdStatus, 84, y, kPropertiesWidth - 96, 20, true);
    y += 26;

    triTransform_[0] = 12;
    triTransform_[1] = y;
    triTransform_[2] = 30;
    triTransform_[3] = y + 22;
    place(kIdTransform, 34, y + 1, 200, 22, true);
    y += 26;

    for (int row = 0; row < 3; ++row) {
        if (!transformOpen_) {
            // Transform plegado: se oculta toda la fila (triangulos
            // tambien, para que no queden zonas de click huerfanas).
            for (int i = 0; i < 4; ++i) triRow_[row][i] = 0;
            place(kIdRowLabel + row, 0, 0, 64, 20, false);
            place(kIdSummary + row, 0, 0, 166, 22, false);
            for (int axis = 0; axis < 3; ++axis) {
                place(kIdAxisLabel + row * 3 + axis, 0, 0, 14, 20, false);
                place(200 + row * 3 + axis, 0, 0, 150, 22, false);
            }
            continue;
        }
        int* tri = triRow_[row];
        tri[0] = 36;
        tri[1] = y + 1;
        tri[2] = 52;
        tri[3] = y + 23;
        place(kIdRowLabel + row, 54, y + 1, 64, 20, true);
        place(kIdSummary + row, 122, y, 166, 22, true);
        y += 24;
        if (!rowOpen_[row]) {
            // Fila plegada: los ejes se ocultan (el resumen sigue).
            for (int axis = 0; axis < 3; ++axis) {
                place(kIdAxisLabel + row * 3 + axis, 0, 0, 14, 20, false);
                place(200 + row * 3 + axis, 0, 0, 150, 22, false);
            }
            continue;
        }
        for (int axis = 0; axis < 3; ++axis) {
            place(kIdAxisLabel + row * 3 + axis, 64, y + 1, 14, 20, true);
            place(200 + row * 3 + axis, 84, y, 150, 22, true);
            y += 24;
        }
    }

    y += 2;
    place(kIdParentLabel, 12, y + 2, 70, 20, true);
    place(kIdParentValue, 84, y + 2, kPropertiesWidth - 96, 20, true);
    InvalidateRect(props, nullptr, TRUE);
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

            // Triangulitos de las cascadas del Transform (solo en el
            // panel de Properties; las zonas huerfanas van a cero).
            if (self && hwnd == static_cast<HWND>(self->properties_)) {
                HFONT oldFont =
                    static_cast<HFONT>(SelectObject(hdc, theme::uiFont()));
                SetBkMode(hdc, TRANSPARENT);
                drawTriangle(hdc, self->triTransform_, self->transformOpen_);
                if (self->transformOpen_) {
                    for (int row = 0; row < 3; ++row) {
                        drawTriangle(hdc, self->triRow_[row],
                                     self->rowOpen_[row]);
                    }
                }
                SelectObject(hdc, oldFont);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            // Click en un triangulito: alterna la cascada y recoloca.
            if (self && hwnd == static_cast<HWND>(self->properties_)) {
                const int x = static_cast<short>(LOWORD(lParam));
                const int y = static_cast<short>(HIWORD(lParam));
                if (inTri(self->triTransform_, x, y)) {
                    self->transformOpen_ = !self->transformOpen_;
                    self->layoutProperties();
                    return 0;
                }
                if (self->transformOpen_) {
                    for (int row = 0; row < 3; ++row) {
                        if (inTri(self->triRow_[row], x, y)) {
                            self->rowOpen_[row] = !self->rowOpen_[row];
                            self->layoutProperties();
                            return 0;
                        }
                    }
                }
            }
            break;
        }
        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (ui::paintDarkButton(*dis)) return TRUE;
            break;
        }
        case WM_COMMAND: {
            if (!self) break;
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);
            // Campo Transform editado: al perder el foco se aplica una
            // sola vez (no en cada tecla) y el dueno guarda la escena.
            if (id >= 200 && id <= 208 && code == EN_KILLFOCUS &&
                self->onTransformEdited_) {
                HWND field =
                    GetDlgItem(static_cast<HWND>(self->properties_), id);
                char text[64]{};
                if (field) {
                    GetWindowTextA(field, text,
                                   static_cast<int>(sizeof(text)));
                }
                // Teclado español: la coma decimal tambien vale.
                for (char& c : text) {
                    if (c == ',') c = '.';
                }
                char* end = nullptr;
                const float value = strtof(text, &end);
                const bool valid = end != text;
                if (valid && !self->selectedName_.empty()) {
                    self->onTransformEdited_(self->selectedName_, id, value);
                } else if (field) {
                    // No es un numero o no hay objeto: texto anterior.
                    SetWindowTextA(field, self->fieldText_[id - 200].c_str());
                }
                return 0;
            }
            // Resumen "x, y, z" (ids 300..302): al perder el foco se
            // aplican los tres ejes de la fila de una vez.
            if (id >= kIdSummary && id <= kIdSummary + 2 &&
                code == EN_KILLFOCUS && self->onTransformEdited_) {
                HWND field =
                    GetDlgItem(static_cast<HWND>(self->properties_), id);
                char text[96]{};
                if (field) {
                    GetWindowTextA(field, text,
                                   static_cast<int>(sizeof(text)));
                }
                float values[3]{};
                if (parseTriple(text, values) && !self->selectedName_.empty()) {
                    const int row = id - kIdSummary;
                    for (int axis = 0; axis < 3; ++axis) {
                        self->onTransformEdited_(self->selectedName_,
                                                 200 + row * 3 + axis,
                                                 values[axis]);
                    }
                } else if (field) {
                    SetWindowTextA(field,
                                   self->summaryText_[id - kIdSummary].c_str());
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
            // Los valores solo lectura (Nombre y Parent, STATIC) y los
            // "-" de estados vacios van apagados; las etiquetas en
            // color normal de texto.
            const int ctlId = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
            char text[64]{};
            GetWindowTextA(reinterpret_cast<HWND>(lParam), text, 64);
            const bool muted = ctlId == kIdStatus ||
                               ctlId == kIdParentValue ||
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
