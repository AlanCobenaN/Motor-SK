#include "docs_view.h"

#include <windows.h>
#include <commctrl.h>

#include <cstring>

#include "../core/log.h"
#include "theme.h"
#include "workspace_tabs.h"

namespace sk {

namespace {

const char* kDocsClass = "MotorSKDocsView";

enum ControlId {
    kIdClassList = 1,
    kIdPropsList = 2,
};

// Lista de clases del motor, agrupadas por categoria (18 categorias). El
// "logo" es un glifo ASCII corto derivado de la categoria.
struct ClassDef {
    const char* category;
    const char* name;
    const char* shortDesc;
    const char* longDesc;
};

const ClassDef kClasses[] = {
    // 1. Contenedores de la escena.
    {"Contenedor", "Workspace", "Contenedor raiz de la escena 3D.",
     "Workspace es el contenedor raiz donde viven todas las partes, luces "
     "y modelos de la escena. Se crea automaticamente al abrir un lugar."},
    {"Contenedor", "Camera", "Punto de vista de la escena.",
     "Camera define la posicion y orientacion desde la que se observa el "
     "Workspace. Tiene propiedades como FieldOfView y CFrame."},
    {"Contenedor", "Lighting", "Ajustes globales de iluminacion.",
     "Lighting controla la iluminacion ambiental, la hora del dia y los "
     "efectos atmosfericos de toda la escena."},
    {"Contenedor", "Scripts", "Contenedor de scripts del lugar.",
     "Scripts agrupa los scripts asociados al lugar y organiza su ejecucion."},
    {"Contenedor", "Dimension", "Lugar o escena raiz.",
     "Dimension es la raiz que agrupa Workspace, Camera, Lighting y Scripts. "
     "Sus hijos predeterminados se crean automaticamente."},

    // 2. Partes.
    {"Partes", "Part", "Ladrillo basico de la escena.",
     "Part es la pieza generica del motor: una caja con tamano, color, "
     "material y comportamiento fisico. Todas las formas derivan de ella."},
    {"Partes", "MeshPart", "Parte con malla importada.",
     "MeshPart usa una malla externa como geometria visible, manteniendo la "
     "colision y la fisica de una Part."},
    {"Partes", "Model", "Agrupacion de partes.",
     "Model agrupa varias partes para moverlas, clonarlas o anclarlas como "
     "una sola unidad."},

    // 3. Formas.
    {"Formas", "Block", "Forma cubica.",
     "Block es una forma cubica (ortoedro). Es la forma por defecto al "
     "crear una Part."},
    {"Formas", "Sphere", "Forma esferica.",
     "Sphere es una forma esferica con radio ajustable."},
    {"Formas", "Cylinder", "Forma cilindrica.",
     "Cylinder es una forma cilindrica con radio y altura ajustables."},
    {"Formas", "Wedge", "Forma de cuña.",
     "Wedge es una forma de rampa o cuña triangular."},

    // 4. Luces.
    {"Luces", "Light", "Fuente de luz configurable.",
     "Light anade iluminacion a la escena. Su propiedad Type elige el tipo: "
     "Point, Spot o Surface."},

    // 5. Comportamiento.
    {"Comportamiento", "Script", "Script ejecutado en el servidor.",
     "Script es un bloque de codigo que se ejecuta en el servidor al "
     "iniciarse la partida."},
    {"Comportamiento", "LocalScript", "Script ejecutado en el cliente.",
     "LocalScript se ejecuta en el cliente y puede acceder a la interfaz y "
     "a la entrada del jugador."},
    {"Comportamiento", "ModuleScript", "Modulo de codigo reutilizable.",
     "ModuleScript expone funciones y datos que otros scripts cargan con "
     "require."},

    // 6. Interfaz.
    {"Interfaz", "ScreenGui", "Contenedor de interfaz 2D.",
     "ScreenGui es el lienzo que agrupa los elementos de interfaz dibujados "
     "sobre la pantalla."},
    {"Interfaz", "Frame", "Rectangulo de interfaz.",
     "Frame es un contenedor rectangular con fondo, borde y tamano dentro "
     "de una ScreenGui."},
    {"Interfaz", "TextLabel", "Etiqueta de texto.",
     "TextLabel muestra texto no editable con tipografia y color "
     "configurables."},
    {"Interfaz", "TextButton", "Boton de texto.",
     "TextButton es un elemento pulsable que muestra texto y emite eventos "
     "al hacer clic."},

    // 7. Entrada.
    {"Entrada", "TextBox", "Campo de texto editable.",
     "TextBox permite al jugador escribir texto y notifica los cambios."},

    // 8. Disposicion.
    {"Disposicion", "UIListLayout", "Ordena hijos en fila o columna.",
     "UIListLayout coloca automaticamente los hijos de un contenedor en "
     "una direccion con separacion configurable."},
    {"Disposicion", "UIGridLayout", "Ordena hijos en rejilla.",
     "UIGridLayout coloca automaticamente los hijos en una cuadricula."},

    // 9. Efectos.
    {"Efectos", "ParticleEmitter", "Emisor de particulas.",
     "ParticleEmitter genera particulas para humo, chispas o fuego."},
    {"Efectos", "Beam", "Haz entre dos puntos.",
     "Beam dibuja una banda luminosa entre dos puntos o adjuntos."},

    // 10. Sonido.
    {"Sonido", "Sound", "Fuente de audio.",
     "Sound reproduce un clip de audio en un punto o de forma global."},

    // 11. Terreno.
    {"Terreno", "Terrain", "Terreno editable por vóxeles.",
     "Terrain es el suelo volumetrico editable con las herramientas de "
     "terreno."},
    {"Terreno", "SpawnLocation", "Punto de aparicion.",
     "SpawnLocation marca donde aparecen los jugadores al iniciar."},

    // 12. Fisica.
    {"Fisica", "Attachment", "Punto de anclaje local.",
     "Attachment define un punto en el espacio local de una parte para "
     "unen otros objetos o efectos."},
    {"Fisica", "Motor6D", "Articulacion entre partes.",
     "Motor6D une dos partes y permite animarlas entre si."},

    // 13. Datos.
    {"Datos", "Vector3", "Vector de tres componentes.",
     "Vector3 representa una posicion o direccion con componentes x, y, z."},
    {"Datos", "CFrame", "Matriz de posicion y rotacion.",
     "CFrame combina una posicion y una orientacion para colocar objetos."},
    {"Datos", "Color3", "Color RGB.",
     "Color3 representa un color con componentes rojo, verde y azul."},
    {"Datos", "UDim2", "Tamano/posicion 2D relativa.",
     "UDim2 combina escala y desplazamiento para tamaños y posiciones de "
     "interfaz."},

    // 14. Materiales.
    {"Materiales", "Material", "Material de superficie.",
     "Material define el aspecto (rugosidad, metalicidad, textura) de la "
     "superficie de una parte."},

    // 15. Animacion.
    {"Animacion", "Animation", "Animacion de personajes.",
     "Animation reproduce una pista de animacion sobre un modelo rigueado."},
    {"Animacion", "Animator", "Reproductor de animaciones.",
     "Animator carga y reproduce animaciones en un modelo."},

    // 16. Entorno.
    {"Entorno", "Sky", "Cielo y fondo.",
     "Sky define las texturas del cielo y del horizonte."},
    {"Entorno", "Atmosphere", "Atmosfera y niebla.",
     "Atmosphere anade densa niebla y dispersion atmosferica a la escena."},

    // 17. Servicios.
    {"Servicios", "Players", "Jugadores conectados.",
     "Players gestiona los jugadores conectados y su personaje."},
    {"Servicios", "RunService", "Bucle de ejecucion.",
     "RunService expone eventos por frame para actualizar la logica."},
    {"Servicios", "TweenService", "Interpolacion de valores.",
     "TweenService anima propiedades entre dos valores en el tiempo."},

    // 18. Valores.
    {"Valores", "NumberValue", "Contenedor de un numero.",
     "NumberValue guarda un numero y notifica cuando cambia."},
    {"Valores", "StringValue", "Contenedor de una cadena.",
     "StringValue guarda texto y notifica cuando cambia."},
    {"Valores", "BoolValue", "Contenedor de un booleano.",
     "BoolValue guarda verdadero o falso y notifica cuando cambia."},
};

constexpr int kClassCount =
    static_cast<int>(sizeof(kClasses) / sizeof(kClasses[0]));

// Glifo ASCII corto por categoria, usado como "logo" en la lista.
const char* glyphFor(const char* category) {
    struct Pair {
        const char* category;
        const char* glyph;
    };
    static const Pair kGlyphs[] = {
        {"Contenedor", "[C]"}, {"Partes", "[P]"},   {"Formas", "[F]"},
        {"Luces", "[L]"},      {"Comportamiento", "[S]"}, {"Interfaz", "[G]"},
        {"Entrada", "[T]"},    {"Disposicion", "[D]"}, {"Efectos", "[E]"},
        {"Sonido", "[A]"},     {"Terreno", "[R]"},  {"Fisica", "[x]"},
        {"Datos", "[#]"},      {"Materiales", "[M]"}, {"Animacion", "[>]"},
        {"Entorno", "[O]"},    {"Servicios", "[*]"}, {"Valores", "[=]"},
    };
    for (const Pair& p : kGlyphs) {
        if (std::strcmp(p.category, category) == 0) return p.glyph;
    }
    return "[ ]";
}

} // namespace

long long __stdcall DocsView::wndProc(void* hwndPtr, unsigned int msg,
                                      unsigned long long wParam, long long lParam) {
    HWND hwnd = static_cast<HWND>(hwndPtr);
    DocsView* self =
        reinterpret_cast<DocsView*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        self = static_cast<DocsView*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self) {
        switch (msg) {
            case WM_NOTIFY: {
                auto* nm = reinterpret_cast<NMHDR*>(lParam);
                if (nm->idFrom == kIdClassList &&
                    nm->code == LVN_ITEMCHANGED) {
                    auto* lv = reinterpret_cast<NMLISTVIEW*>(lParam);
                    if ((lv->uNewState & LVIS_SELECTED) &&
                        !(lv->uOldState & LVIS_SELECTED)) {
                        self->sel_ = lv->iItem;
                        self->updateDetails();
                    }
                    return 0;
                }
                break;
            }
            case WM_CTLCOLORSTATIC: {
                HDC hdc = reinterpret_cast<HDC>(wParam);
                SetTextColor(hdc, theme::text());
                SetBkColor(hdc, theme::background());
                return reinterpret_cast<long long>(theme::backgroundBrush());
            }
            default:
                break;
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

bool DocsView::create(void* parentHwnd) {
    parent_ = parentHwnd;

    static bool registered = false;
    if (!registered) {
        HINSTANCE inst = GetModuleHandleW(nullptr);

        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&DocsView::wndProc);
        wc.hInstance = inst;
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.hbrBackground = theme::backgroundBrush();
        wc.lpszClassName = kDocsClass;
        RegisterClassExA(&wc);

        INITCOMMONCONTROLSEX icc{sizeof(icc),
                                 ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&icc);

        registered = true;
    }

    HINSTANCE inst = GetModuleHandleW(nullptr);
    RECT rc{};
    GetClientRect(static_cast<HWND>(parent_), &rc);

    hwnd_ = CreateWindowExA(0, kDocsClass, "Documentacion de clases",
                            WS_CHILD | WS_CLIPCHILDREN, 0, 0, rc.right,
                            rc.bottom, static_cast<HWND>(parent_), nullptr,
                            inst, this);
    if (!hwnd_) {
        SK_ERROR("DocsView: CreateWindowEx fallo (error %lu)", GetLastError());
        return false;
    }
    HWND root = static_cast<HWND>(hwnd_);

    // Lista de clases (izquierda): logo + nombre en la primera columna,
    // descripcion corta en la segunda.
    HWND list = CreateWindowExA(
        0, WC_LISTVIEWA, "",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS |
            WS_BORDER,
        0, 0, kListWidth, 100, root,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdClassList)), inst,
        nullptr);
    list_ = list;
    SendMessageA(list, WM_SETFONT, reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    ListView_SetBkColor(list, theme::listBackground());
    ListView_SetTextBkColor(list, theme::listBackground());
    ListView_SetTextColor(list, theme::text());
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);
    {
        LVCOLUMNA col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<char*>("Clase");
        col.cx = 150;
        ListView_InsertColumn(list, 0, &col);
        col.pszText = const_cast<char*>("Descripcion");
        col.cx = 150;
        ListView_InsertColumn(list, 1, &col);
    }

    // Linea divisoria vertical entre la lista y el detalle.
    divider_ = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE |
                                   SS_OWNERDRAW, kListWidth, 0, 1, 100, root,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(3)),
                               inst, nullptr);

    // Detalle (derecha): nombre de la clase, descripcion larga y tabla de
    // propiedades (vacia de momento).
    title_ = CreateWindowExA(0, "STATIC", "Selecciona una clase",
                             WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 24,
                             root, nullptr, inst, nullptr);
    desc_ = CreateWindowExA(0, "EDIT", "",
                            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
                                ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                            0, 0, 100, 100, root, nullptr, inst, nullptr);
    propsLabel_ = CreateWindowExA(0, "STATIC", "Propiedades",
                                  WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100,
                                  20, root, nullptr, inst, nullptr);
    HWND props = CreateWindowExA(
        0, WC_LISTVIEWA, "",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_BORDER, 0, 0,
        100, 100, root,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdPropsList)), inst,
        nullptr);
    props_ = props;
    ListView_SetBkColor(props, theme::listBackground());
    ListView_SetTextBkColor(props, theme::listBackground());
    ListView_SetTextColor(props, theme::text());
    {
        LVCOLUMNA col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<char*>("Propiedad");
        col.cx = 180;
        ListView_InsertColumn(props, 0, &col);
        col.pszText = const_cast<char*>("Valor por defecto");
        col.cx = 200;
        ListView_InsertColumn(props, 1, &col);
    }

    void* labels[] = {title_, desc_, propsLabel_};
    for (void* h : labels) {
        SendMessageA(static_cast<HWND>(h), WM_SETFONT,
                     reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    }

    buildClassList();
    layout(rc.right, rc.bottom);
    SK_INFO("Vista de documentacion de clases lista");
    return true;
}

void DocsView::destroy() {
    if (hwnd_) {
        DestroyWindow(static_cast<HWND>(hwnd_));
        hwnd_ = nullptr;
    }
    list_ = nullptr;
    divider_ = nullptr;
    title_ = nullptr;
    desc_ = nullptr;
    propsLabel_ = nullptr;
    props_ = nullptr;
}

void DocsView::setVisible(bool visible) {
    visible_ = visible;
    if (hwnd_) ShowWindow(static_cast<HWND>(hwnd_), visible ? SW_SHOW : SW_HIDE);
}

void DocsView::resize(int width, int height) { layout(width, height); }

void DocsView::buildClassList() {
    HWND list = static_cast<HWND>(list_);
    if (!list) return;
    ListView_DeleteAllItems(list);
    classes_.clear();

    for (int i = 0; i < kClassCount; ++i) {
        const ClassDef& c = kClasses[i];
        classes_.push_back({c.name, c.category, c.shortDesc, c.longDesc});

        // El "logo" es un glifo ASCII derivado de la categoria.
        char logo[16]{};
        const char* g = glyphFor(c.category);
        int gi = 0;
        while (g[gi] != '\0' && gi < static_cast<int>(sizeof(logo)) - 1) {
            logo[gi] = g[gi];
            ++gi;
        }
        logo[gi] = '\0';

        LVITEMA item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = logo;
        ListView_InsertItem(list, &item);
        ListView_SetItemText(list, i, 1,
                             const_cast<char*>(c.shortDesc));
    }
    sel_ = 0;
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    updateDetails();
}

void DocsView::updateDetails() {
    if (sel_ < 0 || sel_ >= static_cast<int>(classes_.size())) return;
    const ClassInfo& c = classes_[sel_];
    if (title_) {
        SetWindowTextA(static_cast<HWND>(title_), c.className);
    }
    if (desc_) {
        SetWindowTextA(static_cast<HWND>(desc_),
                       c.longDesc ? c.longDesc : c.shortDesc);
    }
    // La tabla "Propiedades" se completa en una iteracion posterior; de
    // momento queda vacia.
    if (props_) {
        ListView_DeleteAllItems(static_cast<HWND>(props_));
    }
}

void DocsView::layout(int width, int height) {
    if (!hwnd_) return;
    const int y = WorkspaceTabs::kTopBandHeight;
    int h = height - y;
    if (h < 0) h = 0;
    MoveWindow(static_cast<HWND>(hwnd_), 0, y, width, h, TRUE);

    if (h <= 0) return;

    const int detailX = kListWidth + 12;
    const int detailW = width - detailX - 12;
    const int descH = 120;

    if (list_) {
        MoveWindow(static_cast<HWND>(list_), 0, 0, kListWidth, h, TRUE);
    }
    if (divider_) {
        MoveWindow(static_cast<HWND>(divider_), kListWidth, 0, 1, h, TRUE);
    }
    if (title_) {
        MoveWindow(static_cast<HWND>(title_), detailX, 10, detailW, 26, TRUE);
    }
    if (desc_) {
        MoveWindow(static_cast<HWND>(desc_), detailX, 42, detailW, descH, TRUE);
    }
    if (propsLabel_) {
        MoveWindow(static_cast<HWND>(propsLabel_), detailX, 42 + descH + 12,
                   detailW, 20, TRUE);
    }
    if (props_) {
        const int propsY = 42 + descH + 36;
        int propsH = h - propsY - 12;
        if (propsH < 40) propsH = 40;
        MoveWindow(static_cast<HWND>(props_), detailX, propsY, detailW, propsH,
                   TRUE);
    }
}

} // namespace sk
