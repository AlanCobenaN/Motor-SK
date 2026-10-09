#include "place_view.h"

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
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
constexpr int kIdPivotSummary = 310;  // 310..311 (resumen x,y,z del Pivot)

// Appearance
constexpr int kIdCastShadow = 400;
constexpr int kIdReflectance = 410;
constexpr int kIdTransparency = 411;

// Data
constexpr int kIdClassName = 420;
constexpr int kIdLocked = 401;
// Appearance: forma geometrica (solo lectura)
constexpr int kIdShape = 421;

// Collision
constexpr int kIdCanCollide = 402;
constexpr int kIdAnchored = 403;

// Pivot
constexpr int kIdPivotBase = 500;  // 500..505: Position (500-502) + Orientation (503-505)

// Etiquetas de las categorias nuevas. No pueden valer 0: GetDlgItem
// devolveria siempre la misma y place() solo moveria una de ellas.
constexpr int kIdLblClassName = 600;
constexpr int kIdLblAppearance = 601;
constexpr int kIdLblCastShadow = 602;
constexpr int kIdLblReflectance = 603;
constexpr int kIdLblTransparency = 604;
constexpr int kIdLblData = 605;
constexpr int kIdLblLocked = 606;
constexpr int kIdLblCollision = 607;
constexpr int kIdLblCanCollide = 608;
constexpr int kIdLblAnchored = 609;
constexpr int kIdLblPivot = 610;
constexpr int kIdLblPivotRow = 611;   // 611..612: Position / Orientation
constexpr int kIdLblPivotAxis = 620;  // 620..625: X / Y / Z de cada fila
constexpr int kIdLblShape = 626;      // etiqueta "Shape" de Appearance

// Indices de las categorias del panel Properties (orden visual y de
// las cascadas en catOpen_/triCat_).
constexpr int kCatAppearance = 0;
constexpr int kCatData = 1;
constexpr int kCatTransform = 2;
constexpr int kCatPivot = 3;
constexpr int kCatCollision = 4;

// Sombras de fondo de las cajitas del panel Properties (RowBand::shade):
// base de la cajita, fila alterna mas clara y cabecera de la cajita.
constexpr int kShadeBox = 0;
constexpr int kShadeAlt = 1;
constexpr int kShadeHeader = 2;

// Pincel del sombreado de una banda (base de cajita por defecto).
HBRUSH shadeBrush(int shade) {
    switch (shade) {
        case kShadeAlt: return theme::boxBackgroundAltBrush();
        case kShadeHeader: return theme::boxHeaderBrush();
        default: return theme::boxBackgroundBrush();
    }
}

// Banda que contiene el punto (x, y) del panel en coordenadas de
// contenido; -1 si el punto cae fuera de cualquier cajita.
int bandShadeAt(const std::vector<RowBand>& bands, int x, int y) {
    for (const RowBand& b : bands) {
        if (x >= b.rc.left && x < b.rc.right && y >= b.rc.top &&
            y < b.rc.bottom) {
            return b.shade;
        }
    }
    return -1;
}

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

// Busca un nodo por texto recorriendo el arbol completo (por debajo de
// item en profundidad). Devuelve nullptr si no existe.
HTREEITEM findTreeItemRec(HWND tree, HTREEITEM item, const char* name) {
    while (item) {
        char text[64]{};
        TVITEMA tvi{};
        tvi.hItem = item;
        tvi.mask = TVIF_TEXT;
        tvi.pszText = text;
        tvi.cchTextMax = static_cast<int>(sizeof(text));
        TreeView_GetItem(tree, &tvi);
        if (std::strcmp(text, name) == 0) return item;

        HTREEITEM child = TreeView_GetChild(tree, item);
        if (child) {
            HTREEITEM hit = findTreeItemRec(tree, child, name);
            if (hit) return hit;
        }
        item = TreeView_GetNextSibling(tree, item);
    }
    return nullptr;
}

HTREEITEM findTreeItem(HWND tree, const char* name) {
    return findTreeItemRec(tree, TreeView_GetRoot(tree), name);
}

// Escribe un campo Transform (ids 200..208) con formato compacto.
// keep (opcional) guarda el texto escrito para poder restaurarlo si el
// usuario escribe algo que no es un numero. Si el campo ya muestra ese
// texto no se reescribe (evita repintar el EDIT y con ello parpadeos).
void setTransformField(HWND props, int id, float value,
                       std::string* keep = nullptr) {
    HWND field = GetDlgItem(props, id);
    if (!field) return;
    char buf[32]{};
    snprintf(buf, sizeof(buf), "%.3g", value);
    if (keep) {
        if (*keep == buf) {
            char cur[32]{};
            GetWindowTextA(field, cur, static_cast<int>(sizeof(cur)));
            if (std::strcmp(cur, buf) == 0) return;
        }
        *keep = buf;
    }
    SetWindowTextA(field, buf);
}

// Escribe un STATIC/EDIT de solo lectura solo si el texto cambia: asi
// un refresco por frame (arrastre) no repinta controles que no cambiaron.
void setWindowTextIfDifferent(HWND w, const char* text) {
    if (!w || !text) return;
    char cur[96]{};
    GetWindowTextA(w, cur, static_cast<int>(sizeof(cur)));
    if (std::strcmp(cur, text) != 0) SetWindowTextA(w, text);
}

// Transform identico para raiz / sin seleccion: posicion 0, size 1,
// orientacion 0. keep apunta a los 9 textos guardados (nullptr para
// saltar). Las filas son Position(200..202), Size(203..205) y
// Orientation(206..208).
void setTransformIdentity(HWND props, std::string* keep) {
    for (int i = 0; i < 3; ++i) {
        setTransformField(props, 200 + i, 0.0f, keep ? &keep[i] : nullptr);
        setTransformField(props, 203 + i, 1.0f,
                          keep ? &keep[3 + i] : nullptr);
        setTransformField(props, 206 + i, 0.0f,
                          keep ? &keep[6 + i] : nullptr);
    }
}

HWND makeStatic(HWND parent, HINSTANCE inst, int id, const char* text,
                int x, int y, int w, int h, HFONT font) {
    HWND hwnd = CreateWindowExA(0, "STATIC", text,
                                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY,
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
// plegada, abajo si esta desplegada. rect es la zona de click; el
// triangulo dibujado es proporcional a la zona (con un minimo de 6x4)
// para que se vea sin dominar la fila.
void drawTriangle(HDC hdc, const int rect[4], bool open) {
    if (rect[2] <= rect[0] || rect[3] <= rect[1]) return;
    const int cx = (rect[0] + rect[2]) / 2;
    const int cy = (rect[1] + rect[3]) / 2;
    int hw = (rect[2] - rect[0] - 4) / 2;
    int hh = (rect[3] - rect[1] - 4) / 2;
    if (hw < 3) hw = 3;
    if (hh < 2) hh = 2;
    if (hw > 6) hw = 6;
    if (hh > 5) hh = 5;
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

// Checkbox de los booleanos: cajita oscura dibujada a mano (BS_OWNERDRAW)
// para que el fondo no sea el gris del sistema. bg es el pincel de la
// banda zebra que hay debajo de la casilla; el estado lo trae el dueno
// (BM_GETCHECK requiere BS_CHECKBOX, que no combina con BS_OWNERDRAW).
void drawCheckBox(const DRAWITEMSTRUCT& dis, HBRUSH bg, bool checked) {
    HDC hdc = dis.hDC;
    RECT r = dis.rcItem;
    FillRect(hdc, &r, bg);

    const int side = 14;
    const int cx = (r.left + r.right) / 2;
    const int cy = (r.top + r.bottom) / 2;
    RECT box{cx - side / 2, cy - side / 2, cx + side / 2 + 1,
             cy + side / 2 + 1};

    HBRUSH fill = CreateSolidBrush(theme::surface());
    FillRect(hdc, &box, fill);
    DeleteObject(fill);
    HBRUSH frame =
        CreateSolidBrush(checked ? theme::text() : theme::border());
    FrameRect(hdc, &box, frame);
    DeleteObject(frame);

    if (checked) {
        HPEN pen = CreatePen(PS_SOLID, 2, theme::text());
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
        MoveToEx(hdc, box.left + 3, cy, nullptr);
        LineTo(hdc, box.left + 6, box.bottom - 4);
        LineTo(hdc, box.right - 3, box.top + 4);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }
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
                                  WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL,
                                  0, 0, kPropertiesWidth, 100, parent, nullptr,
                                  inst, this);
    if (!explorer_ || !properties_) {
        SK_ERROR("PlaceView: CreateWindowEx fallo (error %lu)", GetLastError());
        return false;
    }

    // Explorer: arbol oscuro con la raiz dimension01. Sin los botones
    // +/- nativos (TVS_HASBUTTONS): los triangulitos de cascada los
    // pinta la subclase del arbol para igualar el estilo de Properties.
    HWND tree = CreateWindowExA(0, WC_TREEVIEWA, "",
                                WS_CHILD | WS_VISIBLE |
                                TVS_HASLINES | TVS_LINESATROOT |
                                TVS_SHOWSELALWAYS,
                                6, kHeaderHeight + 6, kExplorerWidth - 12, 100,
                                static_cast<HWND>(explorer_),
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(1)),
                                inst, nullptr);
    tree_ = tree;
    SendMessageA(tree, WM_SETFONT, reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    TreeView_SetBkColor(tree, theme::listBackground());
    TreeView_SetTextColor(tree, theme::text());
    TreeView_SetLineColor(tree, theme::border());
    SetWindowSubclass(tree,
                      reinterpret_cast<SUBCLASSPROC>(&PlaceView::treeSubclassProc),
                      1, reinterpret_cast<DWORD_PTR>(this));
    // Raiz unica por defecto: la dimension/escena del lugar.
    root_ = addTreeRoot(tree, kRootName);

    // Properties: Nombre (EDIT editable solo con un parte seleccionado)
    // + fila Parent (solo lectura) + Transform (cascada
    // Position/Rotation/Scale, cada fila con su resumen "x, y, z"
    // editable al lado y los tres ejes al desplegarla). Transform
    // arranca abierto y las filas plegadas; layoutProperties coloca y
    // muestra/oculta segun el estado de las cascadas.
    HWND hprop = static_cast<HWND>(properties_);
    makeStatic(hprop, inst, kIdNameLabel, "Name",
               12, kHeaderHeight + 8, 70, 20, theme::uiFont());
    HWND nameField =
        makeEdit(hprop, inst, kIdStatus, "Sin objeto seleccionado",
                 84, kHeaderHeight + 8, kPropertiesWidth - 96, 20);
    EnableWindow(nameField, FALSE); // hasta que haya un objeto seleccionado
    makeStatic(hprop, inst, kIdTransform, "Transform",
               34, 0, 200, 22, theme::uiHeaderFont());

    const char* rowLabels[3] = {"Position", "Size", "Orientation"};
    const char* rowValues[3] = {"0", "1", "0"};
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

    // ClassName (solo lectura)
    makeStatic(hprop, inst, kIdLblClassName, "ClassName",
               12, 0, 70, 20, theme::uiFont());
    makeStatic(hprop, inst, kIdClassName, "-",
               84, 0, kPropertiesWidth - 96, 20, theme::uiFont());

    // Appearance: CastShadow (checkbox), Reflectance (edit), Transparency (edit)
    makeStatic(hprop, inst, kIdLblAppearance, "Appearance",
               34, 0, 200, 22, theme::uiHeaderFont());
    makeStatic(hprop, inst, kIdLblCastShadow, "CastShadow",
               54, 0, 64, 20, theme::uiFont());
    HWND castShadowBtn = CreateWindowExA(0, "BUTTON", "",
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                         122, 0, 20, 20, hprop,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCastShadow)),
                                         inst, nullptr);
    SendMessageA(castShadowBtn, WM_SETFONT, reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    makeStatic(hprop, inst, kIdLblReflectance, "Reflectance",
               54, 0, 64, 20, theme::uiFont());
    makeEdit(hprop, inst, kIdReflectance, "0.00", 122, 0, 166, 22);
    makeStatic(hprop, inst, kIdLblTransparency, "Transparency",
               54, 0, 64, 20, theme::uiFont());
    makeEdit(hprop, inst, kIdTransparency, "0.00", 122, 0, 166, 22);
    // Shape: forma geometrica de la part (solo lectura).
    makeStatic(hprop, inst, kIdLblShape, "Shape",
               54, 0, 70, 20, theme::uiFont());
    makeStatic(hprop, inst, kIdShape, "-",
               84, 0, kPropertiesWidth - 96, 20, theme::uiFont());

    // Data: Locked (checkbox)
    makeStatic(hprop, inst, kIdLblData, "Data",
               34, 0, 200, 22, theme::uiHeaderFont());
    makeStatic(hprop, inst, kIdLblLocked, "Locked",
               54, 0, 64, 20, theme::uiFont());
    HWND lockedBtn = CreateWindowExA(0, "BUTTON", "",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                     122, 0, 20, 20, hprop,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdLocked)),
                                     inst, nullptr);
    SendMessageA(lockedBtn, WM_SETFONT, reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);

    // Collision: CanCollide (checkbox), Anchored (checkbox)
    makeStatic(hprop, inst, kIdLblCollision, "Collision",
               34, 0, 200, 22, theme::uiHeaderFont());
    makeStatic(hprop, inst, kIdLblCanCollide, "CanCollide",
               54, 0, 64, 20, theme::uiFont());
    HWND canCollideBtn = CreateWindowExA(0, "BUTTON", "",
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                         122, 0, 20, 20, hprop,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCanCollide)),
                                         inst, nullptr);
    SendMessageA(canCollideBtn, WM_SETFONT, reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    makeStatic(hprop, inst, kIdLblAnchored, "Anchored",
               54, 0, 64, 20, theme::uiFont());
    HWND anchoredBtn = CreateWindowExA(0, "BUTTON", "",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                       122, 0, 20, 20, hprop,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAnchored)),
                                       inst, nullptr);
    SendMessageA(anchoredBtn, WM_SETFONT, reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);

    // Pivot: Position (3 ejes) + Orientation (3 ejes)
    makeStatic(hprop, inst, kIdLblPivot, "Pivot",
               34, 0, 200, 22, theme::uiHeaderFont());
    const char* pivotLabels[2] = {"Position", "Orientation"};
    for (int row = 0; row < 2; ++row) {
        makeStatic(hprop, inst, kIdLblPivotRow + row, pivotLabels[row],
                   54, 0, 64, 20, theme::uiFont());
        makeEdit(hprop, inst, kIdPivotSummary + row, "0, 0, 0", 122, 0, 166, 22);
        pivotSummaryText_[row] = "0, 0, 0";
        for (int axis = 0; axis < 3; ++axis) {
            makeStatic(hprop, inst, kIdLblPivotAxis + row * 3 + axis,
                       axisNames[axis], 64, 0, 14, 20, theme::uiFont());
            makeEdit(hprop, inst, kIdPivotBase + row * 3 + axis, "0",
                     84, 0, 150, 22);
        }
    }

    layoutProperties();

    resize(rc.right, rc.bottom);
    SK_INFO("Paneles PLACE listos (Properties %d izq + Explorer %d der)",
            kPropertiesWidth, kExplorerWidth);
    return true;
}

void PlaceView::destroy() {
    if (tree_) {
        RemoveWindowSubclass(static_cast<HWND>(tree_),
                             reinterpret_cast<SUBCLASSPROC>(
                                 &PlaceView::treeSubclassProc),
                             1);
    }
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
    dragItem_ = nullptr;
    dropTarget_ = nullptr;
    dragging_ = false;
}

long long __stdcall PlaceView::treeSubclassProc(void* hwndPtr,
                                                unsigned int msg,
                                                unsigned long long wParam,
                                                long long lParam,
                                                unsigned long long subclassId,
                                                unsigned long long refData) {
    (void)subclassId;  // el default (DefSubclassProc) no lo recibe
    HWND hwnd = static_cast<HWND>(hwndPtr);
    PlaceView* self = reinterpret_cast<PlaceView*>(refData);

    // Lee el texto de un nodo en un buffer (para el drag & drop).
    auto readNodeText = [](HWND tree, HTREEITEM item, char* out, int size) {
        out[0] = '\0';
        if (!item) return;
        TVITEMA tvi{};
        tvi.hItem = item;
        tvi.mask = TVIF_TEXT;
        tvi.pszText = out;
        tvi.cchTextMax = size;
        TreeView_GetItem(tree, &tvi);
    };

    switch (msg) {
        case WM_PAINT: {
            // Pinta el arbol y luego los triangulitos de cascada sobre
            // cada nodo visible que tenga hijos (mismo estilo que las
            // categorias de Properties).
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            DefSubclassProc(hwnd, WM_PAINT, reinterpret_cast<WPARAM>(hdc), 0);
            if (self && self->tree_ == hwnd) {
                HTREEITEM item = TreeView_GetFirstVisible(hwnd);
                while (item) {
                    if (TreeView_GetChild(hwnd, item)) {
                        RECT text{};
                        if (TreeView_GetItemRect(hwnd, item, &text, TRUE)) {
                            // Solo los niveles anidados llevan triangulo:
                            // la raiz (nivel 0) lo dejaria cortado.
                            if (text.left >= 14) {
                                const int cy =
                                    (text.top + text.bottom) / 2;
                                const int tri[4] = {text.left - 13, cy - 7,
                                                    text.left - 1, cy + 7};
                                const bool open =
                                    (TreeView_GetItemState(hwnd, item,
                                                           TVIS_EXPANDED) &
                                     TVIS_EXPANDED) != 0;
                                drawTriangle(hdc, tri, open);
                            }
                        }
                    }
                    item = TreeView_GetNextVisible(hwnd, item);
                }
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            // Click en un triangulito de cascada: plegar/desplegar el
            // nodo sin cambiar la seleccion.
            if (self && self->tree_ == hwnd) {
                const int x = static_cast<short>(LOWORD(lParam));
                const int y = static_cast<short>(HIWORD(lParam));
                HTREEITEM item = TreeView_GetFirstVisible(hwnd);
                while (item) {
                    if (TreeView_GetChild(hwnd, item)) {
                        RECT text{};
                        if (TreeView_GetItemRect(hwnd, item, &text, TRUE) &&
                            text.left >= 14) {
                            const int cy = (text.top + text.bottom) / 2;
                            if (x >= text.left - 13 && x < text.left - 1 &&
                                y >= cy - 7 && y < cy + 7) {
                                const bool open =
                                    (TreeView_GetItemState(hwnd, item,
                                                           TVIS_EXPANDED) &
                                     TVIS_EXPANDED) != 0;
                                TreeView_Expand(hwnd, item,
                                                open ? TVE_COLLAPSE
                                                     : TVE_EXPAND);
                                InvalidateRect(hwnd, nullptr, TRUE);
                                return 0;
                            }
                        }
                    }
                    item = TreeView_GetNextVisible(hwnd, item);
                }
            }
            break;
        }
        case WM_MOUSEMOVE: {
            // Durante el arrastre resalta el nodo de destino (lo deja en
            // nulo si el cursor no esta sobre ningun nodo valido).
            if (self && self->tree_ == hwnd && self->dragging_) {
                const int x = static_cast<short>(LOWORD(lParam));
                const int y = static_cast<short>(HIWORD(lParam));
                HTREEITEM target = nullptr;
                TVHITTESTINFO hit{};
                hit.pt.x = x;
                hit.pt.y = y;
                HTREEITEM under = TreeView_HitTest(hwnd, &hit);
                if (under && under != self->dragItem_) target = under;
                if (target != self->dropTarget_) {
                    TreeView_SelectDropTarget(hwnd, target);
                    self->dropTarget_ = target;
                }
                return 0;
            }
            break;
        }
        case WM_LBUTTONUP: {
            // Fin del arrastre: notifica al dueno para re-agrupar.
            if (self && self->tree_ == hwnd && self->dragging_) {
                self->dragging_ = false;
                TreeView_SelectDropTarget(hwnd, nullptr);
                HTREEITEM target = self->dropTarget_;
                HTREEITEM dragged = self->dragItem_;
                self->dropTarget_ = nullptr;
                self->dragItem_ = nullptr;
                ReleaseCapture();

                char childName[64]{};
                char parentName[64]{};
                if (dragged && target && dragged != target &&
                    dragged != static_cast<HTREEITEM>(self->root_)) {
                    if (target == static_cast<HTREEITEM>(self->root_)) {
                        // Suelta sobre dimension01: el arrastrado vuelve
                        // a la raiz (padre vacio).
                        readNodeText(hwnd, dragged, childName,
                                     static_cast<int>(sizeof(childName)));
                    } else {
                        // Soltar X sobre Y: Y queda dentro de X (el
                        // arrastrado absorbe al del cursor).
                        readNodeText(hwnd, target, childName,
                                     static_cast<int>(sizeof(childName)));
                        readNodeText(hwnd, dragged, parentName,
                                     static_cast<int>(sizeof(parentName)));
                    }
                }
                if (childName[0] && self->onReparent_) {
                    self->onReparent_(childName, parentName);
                }
                return 0;
            }
            break;
        }
        default:
            break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// Expande un nodo y todos sus descendientes (para que la jerarquia
// cargada se vea entera al abrir/deshacer). parent es la raiz o un
// nodo cualquiera; expande todos sus hijos en profundidad.
void expandAll(HWND tree, HTREEITEM parent) {
    HTREEITEM child = TreeView_GetChild(tree, parent);
    while (child) {
        TreeView_Expand(tree, child, TVE_EXPAND);
        expandAll(tree, child);
        child = TreeView_GetNextSibling(tree, child);
    }
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
    addObject(name, "");
}

void PlaceView::addObject(const std::string& name,
                          const std::string& parent) {
    if (!tree_ || !root_) return;
    HWND tree = static_cast<HWND>(tree_);

    HTREEITEM hParent = static_cast<HTREEITEM>(root_);
    if (!parent.empty()) {
        HTREEITEM found = findTreeItem(tree, parent.c_str());
        if (found) hParent = found;
    }

    char label[64]{};
    lstrcpynA(label, name.c_str(), 64);
    TVINSERTSTRUCTA ins{};
    ins.hParent = hParent;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT;
    ins.item.pszText = label;
    HTREEITEM item = TreeView_InsertItem(tree, &ins);

    TreeView_Expand(tree, static_cast<HTREEITEM>(root_), TVE_EXPAND);
    if (hParent != static_cast<HTREEITEM>(root_)) {
        TreeView_Expand(tree, hParent, TVE_EXPAND);
    }
    if (item) TreeView_Select(tree, item, TVGN_CARET);
}

void PlaceView::rebuildTree(const std::vector<SceneObject>& objects) {
    clearObjects();
    if (!tree_ || !root_) return;
    HWND tree = static_cast<HWND>(tree_);

    // Inserta nodo y lo apunta por nombre para que los hijos puedan
    // colgar de el (el archivo no garantiza que el padre preceda).
    auto insertNode = [&](HTREEITEM parent, const std::string& name,
                          std::map<std::string, HTREEITEM>& items,
                          HTREEITEM& last) {
        char label[64]{};
        lstrcpynA(label, name.c_str(), 64);
        TVINSERTSTRUCTA ins{};
        ins.hParent = parent;
        ins.hInsertAfter = TVI_LAST;
        ins.item.mask = TVIF_TEXT;
        ins.item.pszText = label;
        HTREEITEM item = TreeView_InsertItem(tree, &ins);
        items[name] = item;
        last = item;
    };

    std::map<std::string, HTREEITEM> items;
    std::vector<const SceneObject*> pending;
    HTREEITEM last = nullptr;
    for (const SceneObject& object : objects) {
        if (object.parent.empty()) {
            insertNode(static_cast<HTREEITEM>(root_), object.name, items, last);
        } else {
            pending.push_back(&object);
        }
    }
    // Pasadas sobre el resto: un hijo se inserta solo cuando su padre
    // ya esta en el arbol.
    for (size_t guard = 0; guard < pending.size() && !pending.empty();
         ++guard) {
        bool progressed = false;
        for (auto it = pending.begin(); it != pending.end();) {
            auto parent = items.find((*it)->parent);
            if (parent != items.end()) {
                insertNode(parent->second, (*it)->name, items, last);
                it = pending.erase(it);
                progressed = true;
            } else {
                ++it;
            }
        }
        if (!progressed) break;
    }
    // Padres huerfanos (el padre no existe): cuelgan de la raiz.
    for (const SceneObject* object : pending) {
        insertNode(static_cast<HTREEITEM>(root_), object->name, items, last);
    }

    // Expande todos los niveles para que la jerarquia se vea entera.
    expandAll(tree, static_cast<HTREEITEM>(root_));
    if (last) TreeView_Select(tree, last, TVGN_CARET);
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

void PlaceView::renameTreeItem(const std::string& oldName,
                               const std::string& newName) {
    if (!tree_ || !root_) return;
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
        if (oldName == text) {
            char label[64]{};
            lstrcpynA(label, newName.c_str(), 64);
            TVITEMA set{};
            set.hItem = child;
            set.mask = TVIF_TEXT;
            set.pszText = label;
            TreeView_SetItem(tree, &set);
            return;
        }
        child = TreeView_GetNextSibling(tree, child);
    }
}

void PlaceView::removeTreeItem(const std::string& name) {
    if (!tree_ || !root_) return;
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
        HTREEITEM next = TreeView_GetNextSibling(tree, child);
        if (name == text) {
            TreeView_DeleteItem(tree, child);
            return;
        }
        child = next;
    }
}

// Estado base de los controles de parte (ClassName, floats, checkboxes y
// Pivot) cuando no hay objeto seleccionado: texto "-" y casillas apagadas.
void resetPartFields(PlaceView* self, HWND props, const char* text,
                     std::string* floatText, std::string* pivotText) {
    if (HWND w = GetDlgItem(props, kIdClassName)) SetWindowTextA(w, text);
    if (HWND w = GetDlgItem(props, kIdShape)) SetWindowTextA(w, text);
    for (int i = 0; i < 2; ++i) {
        if (HWND w = GetDlgItem(props, kIdReflectance + i)) {
            SetWindowTextA(w, text);
        }
        floatText[i] = text;
    }
    for (int i = 0; i < 6; ++i) {
        if (HWND w = GetDlgItem(props, kIdPivotBase + i)) {
            SetWindowTextA(w, text);
        }
        pivotText[i] = text;
    }
    for (int row = 0; row < 2; ++row) {
        if (HWND w = GetDlgItem(props, kIdPivotSummary + row)) {
            SetWindowTextA(w, text);
        }
    }
    for (int id = kIdCastShadow; id <= kIdAnchored; ++id) {
        if (self) self->setCheck(id, false);
    }
}

void PlaceView::setCheck(int id, bool on) {
    const int idx = id - kIdCastShadow;
    if (idx < 0 || idx >= 4) return;
    if (checkState_[idx] == on) return;
    checkState_[idx] = on;
    if (HWND box = GetDlgItem(static_cast<HWND>(properties_), id)) {
        InvalidateRect(box, nullptr, TRUE);
    }
}

void PlaceView::showObject(const SceneObject& object, bool relayout) {
    HWND props = static_cast<HWND>(properties_);
    if (!props) return;
    showCategories_ = true;
    selectedName_ = object.name;
    if (HWND status = GetDlgItem(props, kIdStatus)) {
        setWindowTextIfDifferent(status, object.name.c_str());
        EnableWindow(status, TRUE); // hay objeto: el nombre es editable
    }
    for (int axis = 0; axis < 3; ++axis) {
        setTransformField(props, 200 + axis, (&object.position.x)[axis],
                          &fieldText_[axis]);
        setTransformField(props, 203 + axis, (&object.scale.x)[axis],
                          &fieldText_[3 + axis]);
        setTransformField(props, 206 + axis, (&object.rotation.x)[axis],
                          &fieldText_[6 + axis]);
    }
    updateSummaries();
    if (HWND parent = GetDlgItem(props, kIdParentValue)) {
        // El Parent es la raiz dimension01 salvo que el objeto este
        // anidado bajo otro en el Explorer.
        setWindowTextIfDifferent(
            parent, object.parent.empty() ? kRootName : object.parent.c_str());
    }

    if (HWND className = GetDlgItem(props, kIdClassName)) {
        setWindowTextIfDifferent(className, "Part");
    }
    if (HWND shape = GetDlgItem(props, kIdShape)) {
        setWindowTextIfDifferent(shape, shapeName(object.shape));
    }
    setCheck(kIdCastShadow, object.castShadow);
    if (HWND reflectance = GetDlgItem(props, kIdReflectance)) {
        char buf[32]{};
        snprintf(buf, sizeof(buf), "%.2f", object.reflectance);
        if (floatText_[0] != buf) {
            floatText_[0] = buf;
            SetWindowTextA(reflectance, buf);
        }
    }
    if (HWND transparency = GetDlgItem(props, kIdTransparency)) {
        char buf[32]{};
        snprintf(buf, sizeof(buf), "%.2f", object.transparency);
        if (floatText_[1] != buf) {
            floatText_[1] = buf;
            SetWindowTextA(transparency, buf);
        }
    }
    setCheck(kIdLocked, object.locked);
    setCheck(kIdCanCollide, object.canCollide);
    setCheck(kIdAnchored, object.anchored);
    for (int axis = 0; axis < 3; ++axis) {
        setTransformField(props, kIdPivotBase + axis,
                          (&object.pivotPosition.x)[axis],
                          &pivotText_[axis]);
        setTransformField(props, kIdPivotBase + 3 + axis,
                          (&object.pivotRotation.x)[axis],
                          &pivotText_[3 + axis]);
    }
    updateSummaries(); // resumenes del Pivot ya con los ejes al dia
    // Recoloca y muestra los controles: tras showNoSelection quedaron
    // todos ocultos (showCategories_ = false) y hay que volver a mostrarlos.
    // Mientras se arrastra un objeto el dueno pasa relayout=false y solo
    // se refrescan los numeros, sin recolocar nada (evita el parpadeo).
    if (relayout) layoutProperties();
}

void PlaceView::showRoot() {
    HWND props = static_cast<HWND>(properties_);
    if (!props) return;
    showCategories_ = true;
    selectedName_.clear();
    if (HWND status = GetDlgItem(props, kIdStatus)) {
        SetWindowTextA(status, kRootName);
        EnableWindow(status, FALSE); // la raiz no se renombra aqui
    }
    setTransformIdentity(props, fieldText_);
    updateSummaries();
    if (HWND parent = GetDlgItem(props, kIdParentValue)) {
        SetWindowTextA(parent, "-"); // la raiz no tiene padre
    }
    resetPartFields(this, props, "-", floatText_, pivotText_);
    layoutProperties();
}

void PlaceView::showNoSelection() {
    HWND props = static_cast<HWND>(properties_);
    if (!props) return;
    // Sin parte seleccionada no hay categorias que mostrar.
    showCategories_ = false;
    selectedName_.clear();
    if (HWND status = GetDlgItem(props, kIdStatus)) {
        SetWindowTextA(status, "Sin objeto seleccionado");
        EnableWindow(status, FALSE);
    }
    setTransformIdentity(props, fieldText_);
    updateSummaries();
    if (HWND parent = GetDlgItem(props, kIdParentValue)) SetWindowTextA(parent, "-");
    resetPartFields(this, props, "-", floatText_, pivotText_);
    layoutProperties();
}

void PlaceView::showMultiple(int count) {
    HWND props = static_cast<HWND>(properties_);
    if (!props) return;
    showCategories_ = true;
    selectedName_.clear();
    char text[64]{};
    snprintf(text, sizeof(text), "-%d objetos seleccionados-", count);
    if (HWND status = GetDlgItem(props, kIdStatus)) {
        SetWindowTextA(status, text);
        EnableWindow(status, FALSE); // multi: nada que renombrar
    }
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
    resetPartFields(this, props, "-", floatText_, pivotText_);
    layoutProperties();
}

void PlaceView::syncTreeSelection(const std::string& primary) {
    if (!tree_) return;
    if (primary.empty()) {
        suppressNotify_ = true;
        TreeView_Select(static_cast<HWND>(tree_), nullptr, TVGN_CARET);
        suppressNotify_ = false;
        return;
    }
    // Busca en todo el arbol (los objetos pueden estar anidados).
    HWND tree = static_cast<HWND>(tree_);
    HTREEITEM found = findTreeItem(tree, primary.c_str());
    if (!found) return;
    // Expande el padre para que el caret quede visible.
    HTREEITEM parent = TreeView_GetParent(tree, found);
    if (parent && parent != static_cast<HTREEITEM>(root_)) {
        TreeView_Expand(tree, parent, TVE_EXPAND);
    }
    suppressNotify_ = true;
    TreeView_Select(tree, found, TVGN_CARET);
    suppressNotify_ = false;
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
// textos de los ejes ya guardados en fieldText_. Si el resumen ya
// muestra ese texto no se reescribe (evita repintar el EDIT durante
// un arrastre, que refresca el panel cada frame).
void PlaceView::updateSummaries() {
    HWND props = static_cast<HWND>(properties_);
    if (!props) return;
    for (int row = 0; row < 3; ++row) {
        summaryText_[row] = joinSummary(&fieldText_[row * 3]);
        if (HWND sum = GetDlgItem(props, kIdSummary + row)) {
            char cur[96]{};
            GetWindowTextA(sum, cur, static_cast<int>(sizeof(cur)));
            if (std::strcmp(cur, summaryText_[row].c_str()) != 0) {
                SetWindowTextA(sum, summaryText_[row].c_str());
            }
        }
    }
    // Resumenes "x, y, z" del Pivot (mismas filas Position/Orientation).
    for (int row = 0; row < 2; ++row) {
        pivotSummaryText_[row] = joinSummary(&pivotText_[row * 3]);
        if (HWND sum = GetDlgItem(props, kIdPivotSummary + row)) {
            char cur[96]{};
            GetWindowTextA(sum, cur, static_cast<int>(sizeof(cur)));
            if (std::strcmp(cur, pivotSummaryText_[row].c_str()) != 0) {
                SetWindowTextA(sum, pivotSummaryText_[row].c_str());
            }
        }
    }
}

// Coloca los controles del panel Properties segun el estado de las
// cascadas (Transform y cada fila) y muestra/oculta lo que toque. Las
// filas plegadas siguen mostrando su resumen "x, y, z" editable.
void PlaceView::layoutProperties() {
    HWND props = static_cast<HWND>(properties_);
    if (!props) return;

    // Geometria en coordenadas de contenido (sin el desplazamiento): los
    // controles se mueven luego con -scrollY_ y las bandas/cajitas se
    // pintan en WM_PAINT restando scrollY_.
    const int vscrollW = GetSystemMetrics(SM_CXVSCROLL);
    const int boxX = 6;
    const int boxRight = kPropertiesWidth - vscrollW - 6;
    const int boxW = boxRight - boxX;
    const int labelX = boxX + 22;  // separado del triangulito de cascada
    const int labelW = 82;
    const int valueW = 104;  // campos mas estrechos
    const int valueX = boxRight - valueW - 10;
    // Triangulito de las cascadas: un poco separado del titulo.
    const int triL = boxX + 2;
    const int triR = boxX + 16;
    RECT cli{};
    GetClientRect(props, &cli);
    const int panelH = cli.bottom;

    bands_.clear();
    boxes_.clear();

    struct Pending {
        int id, x, y, w, h;
        bool show;
    };
    std::vector<Pending> pend;
    auto place = [&pend](int id, int x, int y, int w, int h, bool show) {
        pend.push_back(Pending{id, x, y, w, h, show});
    };
    auto band = [&](int y0, int h, int shade) {
        bands_.push_back(RowBand{{boxX, y0, boxX + boxW, y0 + h}, shade});
    };
    auto propRow = [&](int labelId, int valueId, int y0, bool alt, bool show) {
        if (show) {
            band(y0, 22, alt ? kShadeAlt : kShadeBox);
            place(labelId, labelX, y0 + 2, labelW, 20, true);
            place(valueId, valueX, y0, valueW, 20, true);
        } else {
            place(labelId, 0, 0, labelW, 20, false);
            place(valueId, 0, 0, valueW, 20, false);
        }
    };

    int y = kHeaderHeight + 6;
    int boxTop = y;
    // Cabecera de categoria con su triangulito (triCat_[idx]) para
    // plegarla o desplegarla.
    auto beginCat = [&](int idx, int labelId) {
        boxTop = y;
        band(y, 22, kShadeHeader);
        triCat_[idx][0] = triL;
        triCat_[idx][1] = y + 4;
        triCat_[idx][2] = triR;
        triCat_[idx][3] = y + 18;
        place(labelId, labelX, y + 1, boxW - 24, 20, true);
        y += 24;
    };
    auto endCat = [&]() {
        boxes_.push_back(RECT{boxX, boxTop, boxX + boxW, y});
        y += 6;
    };

    // Appearance: CastShadow (bool), Reflectance y Transparency (0..1).
    beginCat(kCatAppearance, kIdLblAppearance);
    {
        const bool show = catOpen_[kCatAppearance];
        propRow(kIdLblCastShadow, kIdCastShadow, y, true, show);
        if (show) y += 22;
        propRow(kIdLblReflectance, kIdReflectance, y, false, show);
        if (show) y += 22;
        propRow(kIdLblTransparency, kIdTransparency, y, true, show);
        if (show) y += 22;
        propRow(kIdLblShape, kIdShape, y, false, show);
        if (show) y += 22;
    }
    endCat();

    // Data: ClassName (solo lectura), Locked (bool), Name y Parent.
    beginCat(kCatData, kIdLblData);
    {
        const bool show = catOpen_[kCatData];
        propRow(kIdLblClassName, kIdClassName, y, false, show);
        if (show) y += 22;
        propRow(kIdLblLocked, kIdLocked, y, true, show);
        if (show) y += 22;
        propRow(kIdNameLabel, kIdStatus, y, false, show);
        if (show) y += 22;
        propRow(kIdParentLabel, kIdParentValue, y, true, show);
        if (show) y += 22;
    }
    endCat();

    // Transform: Position / Size / Orientation, cada fila con su
    // resumen "x, y, z" editable y los tres ejes al desplegarla.
    beginCat(kCatTransform, kIdTransform);
    for (int row = 0; row < 3 && catOpen_[kCatTransform]; ++row) {
        int* tri = triRow_[row];
        tri[0] = triL;
        tri[1] = y + 5;
        tri[2] = triR;
        tri[3] = y + 19;
        band(y, 22, (row % 2 == 0) ? kShadeAlt : kShadeBox);
        place(kIdRowLabel + row, labelX, y + 2, labelW, 20, true);
        place(kIdSummary + row, valueX, y, valueW, 20, true);
        y += 22;
        if (!rowOpen_[row]) {
            for (int axis = 0; axis < 3; ++axis) {
                place(kIdAxisLabel + row * 3 + axis, 0, 0, 14, 20, false);
                place(200 + row * 3 + axis, 0, 0, valueW, 20, false);
            }
            continue;
        }
        for (int axis = 0; axis < 3; ++axis) {
            band(y, 20, kShadeBox);
            place(kIdAxisLabel + row * 3 + axis, valueX - 16, y + 1, 14, 20,
                  true);
            place(200 + row * 3 + axis, valueX, y, valueW, 20, true);
            y += 20;
        }
    }
    if (!catOpen_[kCatTransform]) {
        for (int row = 0; row < 3; ++row) {
            for (int i = 0; i < 4; ++i) triRow_[row][i] = 0;
            place(kIdRowLabel + row, 0, 0, labelW, 20, false);
            place(kIdSummary + row, 0, 0, valueW, 22, false);
            for (int axis = 0; axis < 3; ++axis) {
                place(kIdAxisLabel + row * 3 + axis, 0, 0, 14, 20, false);
                place(200 + row * 3 + axis, 0, 0, valueW, 20, false);
            }
        }
    }
    endCat();

    // Pivot: Position y Orientation (origen de la part), en cascada
    // igual que las filas del Transform.
    beginCat(kCatPivot, kIdLblPivot);
    for (int row = 0; row < 2 && catOpen_[kCatPivot]; ++row) {
        int* tri = triPivotRow_[row];
        tri[0] = triL;
        tri[1] = y + 5;
        tri[2] = triR;
        tri[3] = y + 19;
        band(y, 22, (row % 2 == 0) ? kShadeAlt : kShadeBox);
        place(kIdLblPivotRow + row, labelX, y + 2, labelW, 20, true);
        place(kIdPivotSummary + row, valueX, y, valueW, 20, true);
        y += 22;
        if (!pivotRowOpen_[row]) {
            for (int axis = 0; axis < 3; ++axis) {
                place(kIdLblPivotAxis + row * 3 + axis, 0, 0, 14, 20, false);
                place(kIdPivotBase + row * 3 + axis, 0, 0, valueW, 20, false);
            }
            continue;
        }
        for (int axis = 0; axis < 3; ++axis) {
            band(y, 20, kShadeBox);
            place(kIdLblPivotAxis + row * 3 + axis, valueX - 16, y + 1, 14,
                  20, true);
            place(kIdPivotBase + row * 3 + axis, valueX, y, valueW, 20, true);
            y += 20;
        }
    }
    if (!catOpen_[kCatPivot]) {
        for (int row = 0; row < 2; ++row) {
            for (int i = 0; i < 4; ++i) triPivotRow_[row][i] = 0;
            place(kIdLblPivotRow + row, 0, 0, labelW, 20, false);
            place(kIdPivotSummary + row, 0, 0, valueW, 20, false);
            for (int axis = 0; axis < 3; ++axis) {
                place(kIdLblPivotAxis + row * 3 + axis, 0, 0, 14, 20, false);
                place(kIdPivotBase + row * 3 + axis, 0, 0, valueW, 20, false);
            }
        }
    }
    endCat();

    // Collision: CanCollide y Anchored.
    beginCat(kCatCollision, kIdLblCollision);
    {
        const bool show = catOpen_[kCatCollision];
        propRow(kIdLblCanCollide, kIdCanCollide, y, true, show);
        if (show) y += 22;
        propRow(kIdLblAnchored, kIdAnchored, y, false, show);
        if (show) y += 22;
    }
    endCat();

    // Sin parte seleccionada no se muestra ninguna categoria ni cascada.
    if (!showCategories_) {
        for (Pending& p : pend) p.show = false;
        bands_.clear();
        boxes_.clear();
        for (int i = 0; i < 5; ++i)
            for (int k = 0; k < 4; ++k) triCat_[i][k] = 0;
        for (int i = 0; i < 3; ++i)
            for (int k = 0; k < 4; ++k) triRow_[i][k] = 0;
        for (int i = 0; i < 2; ++i)
            for (int k = 0; k < 4; ++k) triPivotRow_[i][k] = 0;
    }

    // Altura utilizable y scrollbar vertical.
    scrollContent_ = showCategories_ ? (y - (kHeaderHeight + 6)) : 0;
    const int page = panelH - kHeaderHeight > 0 ? panelH - kHeaderHeight : 0;
    const int maxScroll =
        scrollContent_ - page > 0 ? scrollContent_ - page : 0;
    if (scrollY_ > maxScroll) scrollY_ = maxScroll;
    if (scrollY_ < 0) scrollY_ = 0;

    SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS, 0,
                  maxScroll, static_cast<UINT>(page), scrollY_, 0};
    SetScrollInfo(props, SB_VERT, &si, TRUE);
    ShowScrollBar(props, SB_VERT, maxScroll > 0);

    for (const Pending& p : pend) {
        HWND ctl = GetDlgItem(props, p.id);
        if (!ctl) continue;
        SetWindowPos(ctl, nullptr, p.x, p.y - scrollY_, p.w, p.h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(ctl, p.show ? SW_SHOW : SW_HIDE);
    }

    // Repinta el panel y TODOS los hijos para que al mover/ocultar
    // controles no queden restos (lineas) de la disposicion anterior.
    RedrawWindow(props, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void PlaceView::scrollBy(int dy) {
    HWND props = static_cast<HWND>(properties_);
    if (!props) return;
    scrollY_ -= dy;
    if (scrollY_ < 0) scrollY_ = 0;
    layoutProperties();
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

    // El alto del panel cambio: se reenmarca el scroll y se recoloca.
    layoutProperties();
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

            // Cajitas de categorias: bandas zebra de fondo y bordes.
            // Solo en Properties; el Explorer queda plano.
            if (self && hwnd == static_cast<HWND>(self->properties_)) {
                for (const RowBand& b : self->bands_) {
                    RECT r = b.rc;
                    r.top -= self->scrollY_;
                    r.bottom -= self->scrollY_;
                    FillRect(hdc, &r, shadeBrush(b.shade));
                }
                HBRUSH frame = CreateSolidBrush(theme::border());
                for (const RECT& box : self->boxes_) {
                    RECT r = box;
                    r.top -= self->scrollY_;
                    r.bottom -= self->scrollY_;
                    FrameRect(hdc, &r, frame);
                }
                DeleteObject(frame);
            }

            char title[64]{};
            GetWindowTextA(hwnd, title, static_cast<int>(sizeof(title)));
            paintPanelHeader(hdc, rc, title);

            // Triangulitos de las cascadas (solo en Properties; las
            // zonas huerfanas van a cero). Dibuja las 5 categorias y,
            // si estan abiertas, las filas de Transform y Pivot.
            if (self && hwnd == static_cast<HWND>(self->properties_) &&
                self->showCategories_) {
                HFONT oldFont =
                    static_cast<HFONT>(SelectObject(hdc, theme::uiFont()));
                SetBkMode(hdc, TRANSPARENT);
                for (int i = 0; i < 5; ++i) {
                    int tri[4] = {
                        self->triCat_[i][0],
                        self->triCat_[i][1] - self->scrollY_,
                        self->triCat_[i][2],
                        self->triCat_[i][3] - self->scrollY_};
                    drawTriangle(hdc, tri, self->catOpen_[i]);
                }
                if (self->catOpen_[kCatTransform]) {
                    for (int row = 0; row < 3; ++row) {
                        int tri[4] = {
                            self->triRow_[row][0],
                            self->triRow_[row][1] - self->scrollY_,
                            self->triRow_[row][2],
                            self->triRow_[row][3] - self->scrollY_};
                        drawTriangle(hdc, tri, self->rowOpen_[row]);
                    }
                }
                if (self->catOpen_[kCatPivot]) {
                    for (int row = 0; row < 2; ++row) {
                        int tri[4] = {
                            self->triPivotRow_[row][0],
                            self->triPivotRow_[row][1] - self->scrollY_,
                            self->triPivotRow_[row][2],
                            self->triPivotRow_[row][3] - self->scrollY_};
                        drawTriangle(hdc, tri, self->pivotRowOpen_[row]);
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
                // Los triangulos estan en coordenadas de contenido.
                const int cy = y + self->scrollY_;
                for (int i = 0; i < 5; ++i) {
                    if (inTri(self->triCat_[i], x, cy)) {
                        self->catOpen_[i] = !self->catOpen_[i];
                        self->layoutProperties();
                        return 0;
                    }
                }
                if (self->catOpen_[kCatTransform]) {
                    for (int row = 0; row < 3; ++row) {
                        if (inTri(self->triRow_[row], x, cy)) {
                            self->rowOpen_[row] = !self->rowOpen_[row];
                            self->layoutProperties();
                            return 0;
                        }
                    }
                }
                if (self->catOpen_[kCatPivot]) {
                    for (int row = 0; row < 2; ++row) {
                        if (inTri(self->triPivotRow_[row], x, cy)) {
                            self->pivotRowOpen_[row] =
                                !self->pivotRowOpen_[row];
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
            if (dis->CtlType == ODT_BUTTON && dis->CtlID >= kIdCastShadow &&
                dis->CtlID <= kIdAnchored) {
                int shade = -1;
                if (self) {
                    RECT cr{};
                    GetWindowRect(dis->hwndItem, &cr);
                    POINT pt{cr.left + 4, cr.top + 4};
                    MapWindowPoints(nullptr,
                                    static_cast<HWND>(self->properties_), &pt,
                                    1);
                    shade = bandShadeAt(self->bands_, pt.x,
                                        pt.y + self->scrollY_);
                }
                HBRUSH bg = shade == kShadeAlt
                                ? theme::boxBackgroundAltBrush()
                                : (shade == kShadeHeader
                                       ? theme::boxHeaderBrush()
                                       : theme::boxBackgroundBrush());
                const bool checked =
                    self ? self->checkState_[dis->CtlID - kIdCastShadow]
                         : false;
                drawCheckBox(*dis, bg, checked);
                return TRUE;
            }
            if (ui::paintDarkButton(*dis)) return TRUE;
            break;
        }
        case WM_COMMAND: {
            if (!self) break;
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);
            // Campo Nombre (id 100) editado: validar y pedirle al dueno
            // que renombre; si rechaza, se restaura el texto anterior.
            if (id == kIdStatus && code == EN_KILLFOCUS) {
                HWND field =
                    GetDlgItem(static_cast<HWND>(self->properties_), id);
                if (!field || self->selectedName_.empty()) return 0;
                char text[96]{};
                GetWindowTextA(field, text, static_cast<int>(sizeof(text)));
                // Recorte de espacios y caracteres invalidos en nombre
                // de parte: no vacio, sin ':' ni controles.
                char* begin = text;
                while (*begin == ' ' || *begin == '\t') ++begin;
                char* end = begin + std::strlen(begin);
                while (end > begin && (end[-1] == ' ' || end[-1] == '\t')) --end;
                *end = '\0';
                bool ok = *begin != '\0';
                for (const char* p = begin; ok && *p; ++p) {
                    if (*p == ':' || static_cast<unsigned char>(*p) < 32) {
                        ok = false;
                    }
                }
                if (ok && std::strcmp(begin, self->selectedName_.c_str()) == 0) {
                    return 0; // sin cambios
                }
                if (ok && self->onNameEdited_) {
                    ok = self->onNameEdited_(self->selectedName_, begin);
                } else if (ok) {
                    ok = false; // sin dueno: nada que renombrar
                }
                if (!ok) SetWindowTextA(field, self->selectedName_.c_str());
                return 0;
            }
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
            // Resumen "x, y, z" del Pivot (ids 310..311): aplica los tres
            // ejes del origen de una vez.
            if (id >= kIdPivotSummary && id <= kIdPivotSummary + 1 &&
                code == EN_KILLFOCUS && self->onPivotEdited_) {
                HWND field =
                    GetDlgItem(static_cast<HWND>(self->properties_), id);
                char text[96]{};
                if (field) {
                    GetWindowTextA(field, text,
                                   static_cast<int>(sizeof(text)));
                }
                float values[3]{};
                if (parseTriple(text, values) && !self->selectedName_.empty()) {
                    const int row = id - kIdPivotSummary;
                    for (int axis = 0; axis < 3; ++axis) {
                        self->onPivotEdited_(self->selectedName_,
                                             kIdPivotBase + row * 3 + axis,
                                             values[axis]);
                    }
                } else if (field) {
                    SetWindowTextA(
                        field,
                        self->pivotSummaryText_[id - kIdPivotSummary].c_str());
                }
                return 0;
            }
            // Checkbox de Appearance/Data/Collision (400..403): el dueno
            // aplica el bool al objeto y guarda. La casilla es BS_OWNERDRAW
            // (sin BS_CHECKBOX): no alterna sola y el estado lo lleva el
            // panel (checkState_); asi el boton recibe los clicks.
            auto toggleCheck = [&](int checkId) {
                if (checkId < kIdCastShadow || checkId > kIdAnchored) return;
                const int idx = checkId - kIdCastShadow;
                self->checkState_[idx] = !self->checkState_[idx];
                if (HWND box =
                        GetDlgItem(static_cast<HWND>(self->properties_),
                                   checkId)) {
                    InvalidateRect(box, nullptr, TRUE);
                }
                if (self->onBoolEdited_ && !self->selectedName_.empty()) {
                    self->onBoolEdited_(self->selectedName_, checkId,
                                        self->checkState_[idx]);
                }
            };
            if (code == BN_CLICKED && id >= kIdCastShadow &&
                id <= kIdAnchored) {
                toggleCheck(id);
                return 0;
            }
            // La etiqueta de un bool (STATIC) avisa con STN_CLICKED, que
            // comparte valor con BN_CLICKED: pulsar el texto tambien
            // alterna la casilla, como en el propio checkbox.
            if (code == BN_CLICKED) {
                const int labelIds[4] = {kIdLblCastShadow, kIdLblLocked,
                                         kIdLblCanCollide, kIdLblAnchored};
                const int checkIds[4] = {kIdCastShadow, kIdLocked,
                                         kIdCanCollide, kIdAnchored};
                for (int i = 0; i < 4; ++i) {
                    if (id == labelIds[i]) {
                        toggleCheck(checkIds[i]);
                        return 0;
                    }
                }
            }
            // Reflectance/Transparency (410..411): numero 0..1 con dos
            // decimales, aplicado una sola vez al perder el foco.
            if (id >= kIdReflectance && id <= kIdTransparency &&
                code == EN_KILLFOCUS) {
                HWND field =
                    GetDlgItem(static_cast<HWND>(self->properties_), id);
                char text[64]{};
                if (field) {
                    GetWindowTextA(field, text,
                                   static_cast<int>(sizeof(text)));
                }
                for (char& c : text) {
                    if (c == ',') c = '.';
                }
                char* end = nullptr;
                float value = strtof(text, &end);
                if (end != text && !self->selectedName_.empty()) {
                    if (value < 0.0f) value = 0.0f;
                    if (value > 1.0f) value = 1.0f;
                    char buf[32]{};
                    snprintf(buf, sizeof(buf), "%.2f", value);
                    if (field) SetWindowTextA(field, buf);
                    self->floatText_[id - kIdReflectance] = buf;
                    if (self->onFloatEdited_) {
                        self->onFloatEdited_(self->selectedName_, id, value);
                    }
                } else if (field) {
                    SetWindowTextA(
                        field,
                        self->floatText_[id - kIdReflectance].c_str());
                }
                return 0;
            }
            // Campos Pivot (500..505): Position/Orientation del origen.
            if (id >= kIdPivotBase && id <= kIdPivotBase + 5 &&
                code == EN_KILLFOCUS) {
                HWND field =
                    GetDlgItem(static_cast<HWND>(self->properties_), id);
                char text[64]{};
                if (field) {
                    GetWindowTextA(field, text,
                                   static_cast<int>(sizeof(text)));
                }
                for (char& c : text) {
                    if (c == ',') c = '.';
                }
                char* end = nullptr;
                const float value = strtof(text, &end);
                if (end != text && !self->selectedName_.empty()) {
                    char buf[32]{};
                    snprintf(buf, sizeof(buf), "%.3g", value);
                    if (field) SetWindowTextA(field, buf);
                    self->pivotText_[id - kIdPivotBase] = buf;
                    if (self->onPivotEdited_) {
                        self->onPivotEdited_(self->selectedName_, id, value);
                    }
                } else if (field) {
                    SetWindowTextA(field,
                                   self->pivotText_[id - kIdPivotBase].c_str());
                }
                return 0;
            }
            break;
        }
        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<NMHDR*>(lParam);
            if (self && self->tree_ &&
                hdr->hwndFrom == static_cast<HWND>(self->tree_)) {
                if (hdr->code == TVN_SELCHANGEDA ||
                    hdr->code == TVN_SELCHANGEDW) {
                    self->notifySelection();
                    return 0;
                }
                // Inicio del arrastre del Explorer (clic sostenido sobre
                // un nodo): engancha el raton y da control al subclass.
                if ((hdr->code == TVN_BEGINDRAGA ||
                     hdr->code == TVN_BEGINDRAGW) &&
                    !self->dragging_) {
                    HTREEITEM item = nullptr;
                    if (hdr->code == TVN_BEGINDRAGA) {
                        item = reinterpret_cast<LPNMTREEVIEWA>(lParam)
                                   ->itemNew.hItem;
                    } else {
                        item = reinterpret_cast<LPNMTREEVIEWW>(lParam)
                                   ->itemNew.hItem;
                    }
                    if (item &&
                        item != static_cast<HTREEITEM>(self->root_)) {
                        self->dragItem_ = item;
                        self->dropTarget_ = nullptr;
                        self->dragging_ = true;
                        SetCapture(static_cast<HWND>(self->tree_));
                        SetCursor(LoadCursorA(nullptr, IDC_ARROW));
                    }
                    return 0;
                }
                // Al expandir/plegar repinta el arbol para que los
                // triangulitos salgan en su sitio.
                if (hdr->code == TVN_ITEMEXPANDEDA ||
                    hdr->code == TVN_ITEMEXPANDEDW) {
                    InvalidateRect(static_cast<HWND>(self->tree_), nullptr,
                                   TRUE);
                    return 0;
                }
            }
            break;
        }
        case WM_CTLCOLOREDIT: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, theme::surface());
            // El campo Nombre deshabilitado (raiz / sin seleccion /
            // multi) va apagado; el resto de los EDIT en color normal.
            const HWND ctl = reinterpret_cast<HWND>(lParam);
            const bool muted =
                GetDlgCtrlID(ctl) == kIdStatus && !IsWindowEnabled(ctl);
            SetTextColor(hdc, muted ? theme::textDisabled() : theme::text());
            return reinterpret_cast<long long>(theme::surfaceBrush());
        }
        case WM_CTLCOLORBTN: {
            // Checkboxes (BS_OWNERDRAW) del panel: texto en color de
            // tema y fondo de la banda zebra en vez del gris del sistema.
            if (!self) break;
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, theme::text());
            const HWND ctl = reinterpret_cast<HWND>(lParam);
            const HWND props = static_cast<HWND>(self->properties_);
            RECT cr{};
            GetWindowRect(ctl, &cr);
            POINT pt{cr.left + 2, cr.top + 2};
            MapWindowPoints(nullptr, props, &pt, 1);
            const int shade =
                bandShadeAt(self->bands_, pt.x, pt.y + self->scrollY_);
            if (shade >= 0) {
                return reinterpret_cast<long long>(shadeBrush(shade));
            }
            return reinterpret_cast<long long>(theme::backgroundBrush());
        }
        case WM_CTLCOLORSTATIC: {
            if (!self) break;
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            const HWND ctl = reinterpret_cast<HWND>(lParam);
            const HWND props = static_cast<HWND>(self->properties_);
            // Fondo de la banda zebra debajo de esta etiqueta.
            RECT cr{};
            GetWindowRect(ctl, &cr);
            POINT pt{cr.left + 2, cr.top + 2};
            MapWindowPoints(nullptr, props, &pt, 1);
            const int shade =
                bandShadeAt(self->bands_, pt.x, pt.y + self->scrollY_);
            // Los valores solo lectura (Parent, ClassName, STATIC) y los
            // "-" de estados vacios van apagados; las etiquetas en color
            // normal de texto.
            const int ctlId = GetDlgCtrlID(ctl);
            char text[64]{};
            GetWindowTextA(ctl, text, 64);
            const bool muted =
                ctlId == kIdParentValue || ctlId == kIdClassName ||
                ctlId == kIdShape || text[0] == '-';
            SetTextColor(hdc, muted ? theme::textDisabled() : theme::text());
            if (shade >= 0) {
                return reinterpret_cast<long long>(shadeBrush(shade));
            }
            return reinterpret_cast<long long>(theme::backgroundBrush());
        }
        case WM_VSCROLL: {
            if (!self || hwnd != static_cast<HWND>(self->properties_)) break;
            const int code = LOWORD(wParam);
            RECT cli{};
            GetClientRect(hwnd, &cli);
            const int page =
                cli.bottom - kHeaderHeight > 0 ? cli.bottom - kHeaderHeight : 0;
            if (code == SB_THUMBTRACK || code == SB_THUMBPOSITION) {
                const int pos = HIWORD(wParam);
                self->scrollY_ = pos;
                if (self->scrollY_ < 0) self->scrollY_ = 0;
                self->layoutProperties();
                InvalidateRect(hwnd, nullptr, TRUE);
            } else {
                int dy = 0;
                if (code == SB_LINEUP) dy = 24;
                else if (code == SB_LINEDOWN) dy = -24;
                else if (code == SB_PAGEUP) dy = page;
                else if (code == SB_PAGEDOWN) dy = -page;
                else return 0;
                self->scrollBy(dy);
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (!self || hwnd != static_cast<HWND>(self->properties_)) break;
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (delta == 0) return 0;
            // Tres lineas de 24px por muesca de la rueda.
            self->scrollBy((delta / 120) * 72);
            return 0;
        }
        default:
            break;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

} // namespace sk
