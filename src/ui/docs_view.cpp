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
    kIdDivider = 3,
};

// Categorias de clases (19). "icon" es el emoji provisional que se pinta
// como logo de cada clase; "name" es el nombre en ASCII (sin acentos, como
// el resto de la interfaz).
struct Category {
    const wchar_t* icon;
    const char* name;
};

const Category kCategories[] = {
    {L"\U0001F5C2", "Proyecto y organizacion"},   // 0  cajas
    {L"\U0001F4A1", "Iluminacion y ambiente"},    // 1  bombilla
    {L"\U0001F505", "Fuentes de luz"},            // 2  luz baja
    {L"\u2699", "Fisica y articulaciones"},       // 3  engranaje
    {L"\u2728", "Particulas y efectos"},          // 4  destellos
    {L"\U0001F5B1", "Interaccion"},               // 5  raton
    {L"\U0001F3AC", "Animaciones"},               // 6  claqueta
    {L"\U0001F50A", "Audio"},                     // 7  altavoz
    {L"\U0001F3AE", "Controles de entrada"},      // 8  mando
    {L"\U0001F4E1", "Comunicacion"},              // 9  antena
    {L"\U0001F4DC", "Scripts"},                   // 10 pergamino
    {L"\u23F0", "Tiempo"},                        // 11 despertador
    {L"\U0001F4E6", "Gestion de recursos"},       // 12 paquete
    {L"\U0001F41E", "Diagnostico"},               // 13 bicho
    {L"\U0001F4BE", "Persistencia"},              // 14 disco
    {L"\U0001F6E0", "Edicion en ejecucion"},      // 15 herramientas
    {L"\U0001F5BC", "Interfaz (GUI)"},            // 16 cuadro
    {L"\U0001F300", "Interpolacion"},             // 17 ciclo
    {L"\U0001F522", "Objetos de valor"},          // 18 numeros
};

constexpr int kCategoryCount =
    static_cast<int>(sizeof(kCategories) / sizeof(kCategories[0]));

// Clase del motor: categoria (indice en kCategories), nombre y descripcion
// corta.
struct ClassDef {
    int cat;
    const char* name;
    const char* shortDesc;
};

const ClassDef kClasses[] = {
    // 0. Proyecto y organizacion.
    {0, "Dimension", "Dimension independiente con sus objetos, config y scripts."},
    {0, "Workspace", "Contiene los objetos del mundo de una dimension."},
    {0, "Part", "Objeto 3D basico con forma, transformacion y colision."},
    {0, "Model", "Agrupa objetos para manipularlos como una unidad."},
    {0, "Folder", "Organiza objetos jerarquicamente."},
    {0, "MeshPart", "Geometria basada en una malla 3D."},
    {0, "Decal", "Aplica una imagen sobre una superficie."},
    {0, "Texture", "Repite una imagen en mosaico sobre una superficie."},
    {0, "Color", "Color aplicable a un objeto o a parte de su geometria."},
    {0, "Accessory", "Accesorio configurable."},
    {0, "Attachment", "Punto de referencia para uniones y efectos."},
    {0, "Camera", "Vista de juego de una dimension."},
    {0, "CollectionService", "Busca objetos por etiqueta y avisa de cambios."},

    // 1. Iluminacion y ambiente.
    {1, "Lighting", "Configura la iluminacion y el ambiente de una dimension."},
    {1, "Sky", "Define la apariencia del cielo."},
    {1, "Atmosphere", "Dispersion atmosferica y niebla."},
    {1, "Clouds", "Representa y configura las nubes."},
    {1, "BloomEffect", "Resplandor alrededor de zonas luminosas."},
    {1, "BlurEffect", "Aplica desenfoque."},
    {1, "ColorCorrectionEffect", "Modifica color, contraste y saturacion."},
    {1, "ColorGradingEffect", "Aplica gradacion de color."},
    {1, "DepthOfFieldEffect", "Desenfoca segun la distancia a la camara."},
    {1, "SunRaysEffect", "Simula rayos solares."},

    // 2. Fuentes de luz.
    {2, "Light", "Fuente de luz; Type: Point, Spot o Surface."},

    // 3. Fisica, movimiento y articulaciones.
    {3, "AlignOrientation", "Controla o alinea la orientacion de un objeto."},
    {3, "AngularVelocity", "Controla la velocidad angular."},
    {3, "LinearVelocity", "Controla la velocidad lineal."},
    {3, "VectorForce", "Aplica una fuerza vectorial."},
    {3, "BallSocketConstraint", "Rotacion en varias direcciones entre cuerpos."},
    {3, "HingeConstraint", "Permite rotacion alrededor de un eje."},
    {3, "NoCollisionConstraint", "Evita la colision entre dos objetos."},
    {3, "Weld", "Conecta rigidamente dos objetos."},
    {3, "WeldConstraint", "Mantiene dos objetos unidos sin movimiento."},
    {3, "RopeConstraint", "Cuerda que limita la separacion entre puntos."},
    {3, "AirController", "Gestiona el control del movimiento en el aire."},
    {3, "Humanoid", "Controlador de personaje con vida y movimiento."},
    {3, "Seat", "Permite que un personaje se siente."},
    {3, "SpawnLocation", "Define un punto de aparicion."},
    {3, "Pathfinding", "Navegacion y busqueda de rutas."},

    // 4. Particulas y efectos.
    {4, "ParticleEmitter", "Genera particulas configurables."},
    {4, "Trail", "Rastro visual detras de un objeto en movimiento."},
    {4, "Smoke", "Genera humo."},
    {4, "Fire", "Efecto visual de fuego."},
    {4, "Sparkles", "Genera destellos."},
    {4, "Explosion", "Explosion visual con efectos fisicos."},
    {4, "Highlight", "Resalta visualmente un objeto."},
    {4, "Beam", "Efecto visual entre dos puntos."},

    // 5. Interaccion.
    {5, "ClickDetector", "Detecta clics sobre un objeto."},
    {5, "DragDetector", "Permite arrastrar objetos."},
    {5, "ProximityPrompt", "Interacciones contextuales al acercarse."},
    {5, "Dialog", "Dialogo interactivo."},
    {5, "DialogChoice", "Opcion seleccionable de un dialogo."},

    // 6. Animaciones.
    {6, "Animation", "Recurso de animacion."},
    {6, "AnimationConstraint", "Restricciones del sistema de animacion."},
    {6, "Bone", "Hueso de una estructura articulada."},
    {6, "Motor6D", "Conecta partes articuladas y controla su transformacion."},
    {6, "Keyframe", "Fotograma clave."},
    {6, "KeyframeSequence", "Secuencia de fotogramas clave."},
    {6, "EulerRotationCurve", "Curva de rotacion con angulos de Euler."},
    {6, "RotationCurve", "Curva de orientacion."},
    {6, "Vector3Curve", "Curva de valores vectoriales."},
    {6, "Animator", "Coordina la reproduccion de animaciones."},

    // 7. Audio.
    {7, "Sound", "Reproduce musica, voces y efectos."},
    {7, "SoundEffect", "Clase base abstracta de efectos de audio."},
    {7, "SoundGroup", "Agrupa sonidos con configuracion compartida."},
    {7, "SoundService", "Configuracion general y reproduccion de audio."},
    {7, "ChorusSoundEffect", "Efecto de coro."},
    {7, "CompressorSoundEffect", "Comprime el rango dinamico."},
    {7, "DistortionSoundEffect", "Anade distorsion o saturacion."},
    {7, "EchoSoundEffect", "Genera repeticiones de sonido."},
    {7, "EqualizerSoundEffect", "Modifica bandas de frecuencia."},
    {7, "FlangeSoundEffect", "Efecto de modulacion tipo flanger."},
    {7, "PitchShiftSoundEffect", "Modifica el tono del sonido."},
    {7, "ReverbSoundEffect", "Simula la reverberacion de un espacio."},
    {7, "TremoloSoundEffect", "Modula periodicamente el volumen."},
    {7, "Wire", "Conecta componentes de una cadena de audio."},

    // 8. Controles de entrada.
    {8, "Mouse", "Gestiona las entradas del raton."},
    {8, "KeyboardControl", "Control configurable asociado a una tecla."},
    {8, "GamepadControl", "Control configurable de mando."},
    {8, "TouchControl", "Control configurable para pantallas tactiles."},

    // 9. Comunicacion entre componentes.
    {9, "BindableEvent", "Eventos para componentes del mismo contexto."},
    {9, "BindableFunction", "Llama a una funcion de otro componente."},
    {9, "RemoteEvent", "Eventos entre contextos separados."},
    {9, "RemoteFunction", "Llamadas con respuesta entre contextos separados."},

    // 10. Scripts y actualizacion.
    {10, "Script", "Codigo que se ejecuta segun su ubicacion."},
    {10, "LocalScript", "Codigo que se ejecuta del lado del jugador."},
    {10, "ModuleScript", "Codigo reutilizable que otros scripts importan."},
    {10, "Heartbeat", "Evento de actualizacion para los scripts."},
    {10, "Stepped", "Evento de la actualizacion de la simulacion."},

    // 11. Tiempo.
    {11, "ClockTime", "Consulta y manipula el tiempo de un ciclo temporal."},

    // 12. Gestion de recursos.
    {12, "Debris", "Programa la eliminacion de objetos tras un tiempo."},
    {12, "CacheService", "Gestiona recursos almacenados temporalmente."},
    {12, "AssetService", "Administra recursos y operaciones con assets."},
    {12, "ContentProvider", "Gestiona la precarga de recursos."},

    // 13. Diagnostico y depuracion.
    {13, "LogService", "Centraliza registros, avisos y mensajes."},
    {13, "ErrorReporter", "Recopila informacion sobre errores."},
    {13, "Stats", "Estadisticas de rendimiento y recursos."},
    {13, "DebugSettings", "Opciones de configuracion para depuracion."},

    // 14. Persistencia y guardado local.
    {14, "DataStore", "Almacena y recupera datos persistentes."},
    {14, "DataStoreService", "Administra los almacenes de datos."},
    {14, "JSONService", "Convierte datos a JSON y desde JSON."},
    {14, "SaveService", "Coordina el guardado y la carga."},

    // 15. Edicion de recursos en ejecucion.
    {15, "EditableImage", "Modifica imagenes durante la ejecucion."},
    {15, "EditableMesh", "Crea o modifica geometria durante la ejecucion."},

    // 16. Interfaz grafica (GUI).
    {16, "GuiObject", "Clase base abstracta de elementos visuales."},
    {16, "ScreenGui", "Interfaz 2D que se muestra en pantalla."},
    {16, "Frame", "Contenedor para organizar componentes."},
    {16, "CanvasGroup", "Agrupa elementos con propiedades compartidas."},
    {16, "ScrollingFrame", "Contenedor con desplazamiento."},
    {16, "ImageLabel", "Muestra una imagen."},
    {16, "ImageButton", "Muestra una imagen y permite interaccion."},
    {16, "TextLabel", "Muestra texto."},
    {16, "TextButton", "Boton con texto."},
    {16, "TextBox", "Campo de texto editable."},
    {16, "ViewportFrame", "Escena 3D dentro de la interfaz."},
    {16, "VideoFrame", "Muestra video dentro de la interfaz."},
    {16, "BillboardGui", "Interfaz 3D que mira hacia la camara."},
    {16, "SurfaceGui", "Interfaz dibujada sobre una superficie."},
    {16, "UIComponent", "Clase base abstracta de componentes de interfaz."},
    {16, "UICorner", "Configura las esquinas redondeadas."},
    {16, "UIStroke", "Configura el contorno de un elemento."},
    {16, "UIGradient", "Aplica un degradado de color."},
    {16, "UIShadow", "Anade una sombra visual."},
    {16, "UIPadding", "Define el espacio interior."},
    {16, "UIScale", "Ajusta la escala de un elemento."},
    {16, "UIAspectRatioConstraint", "Mantiene una proporcion de aspecto."},
    {16, "UISizeConstraint", "Limita el tamano minimo y maximo."},
    {16, "UITextSizeConstraint", "Limita el tamano del texto."},
    {16, "UIDragDetector", "Permite arrastrar elementos de interfaz."},
    {16, "UIFlexItem", "Comportamiento flexible en un diseno."},
    {16, "UILayout", "Clase base abstracta de distribucion automatica."},
    {16, "UIListLayout", "Organiza elementos en lista."},
    {16, "UIGridLayout", "Organiza elementos en cuadricula."},
    {16, "UITableLayout", "Organiza elementos en filas y columnas."},
    {16, "UIPageLayout", "Organiza elementos como paginas navegables."},
    {16, "UIGridStyleLayout", "Clase base abstracta de rejilla o lista."},

    // 17. Interpolacion.
    {17, "Tween", "Interpolacion gradual de propiedades."},
    {17, "TweenService", "Coordina la creacion y ejecucion de tweens."},

    // 18. Objetos de valor.
    {18, "StringValue", "Almacena un texto."},
    {18, "IntValue", "Almacena un numero entero."},
    {18, "NumberValue", "Almacena un numero decimal."},
    {18, "BoolValue", "Almacena verdadero o falso."},
    {18, "Vector3Value", "Almacena un vector tridimensional."},
    {18, "Color3Value", "Almacena un color."},
    {18, "ObjectValue", "Almacena una referencia a otro objeto."},
};

constexpr int kClassCount =
    static_cast<int>(sizeof(kClasses) / sizeof(kClasses[0]));

// Colores de la vista (grises acordes al tema oscuro; sin blanco puro).
COLORREF nameColor() { return RGB(214, 214, 220); }
COLORREF descColor() { return RGB(150, 150, 160); }

// Fuente para pintar los emojis (provisional).
HFONT emojiFont() {
    static HFONT font = CreateFontW(
        -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Emoji");
    return font;
}

// Copia una cadena ancha a un buffer fijo.
void copyW(wchar_t* dst, int cap, const wchar_t* src) {
    int i = 0;
    while (src[i] != L'\0' && i < cap - 1) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = L'\0';
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
            case WM_DRAWITEM: {
                auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
                if (dis->CtlID == kIdClassList && dis->itemID != static_cast<UINT>(-1)) {
                    self->paintRow(*dis);
                    return TRUE;
                }
                if (dis->CtlID == kIdDivider) {
                    FillRect(dis->hDC, &dis->rcItem,
                             theme::boxBackgroundBrush());
                    return TRUE;
                }
                break;
            }
            case WM_NOTIFY: {
                auto* nm = reinterpret_cast<NMHDR*>(lParam);
                if (nm->idFrom == kIdClassList && nm->code == LVN_ITEMCHANGED) {
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
                SetTextColor(hdc, nameColor());
                SetBkColor(hdc, theme::listBackground());
                return reinterpret_cast<long long>(theme::listBackgroundBrush());
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
        wc.hbrBackground = theme::listBackgroundBrush();
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

    // Lista de clases (izquierda), owner-draw: icono + nombre + descripcion
    // corta. Las filas se pintan en paintRow.
    HWND list = CreateWindowExA(
        0, WC_LISTVIEWA, "",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDRAWFIXED |
            LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER,
        0, 0, kListWidth, 100, root,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdClassList)), inst,
        nullptr);
    list_ = list;
    SendMessageA(list, WM_SETFONT, reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    ListView_SetBkColor(list, theme::listBackground());
    ListView_SetTextBkColor(list, theme::listBackground());
    ListView_SetTextColor(list, descColor());
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);
    {
        LVCOLUMNA col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<char*>("");
        col.cx = kIconColWidth;
        ListView_InsertColumn(list, 0, &col);
        col.pszText = const_cast<char*>("Clase");
        col.cx = kNameColWidth;
        ListView_InsertColumn(list, 1, &col);
        col.pszText = const_cast<char*>("Descripcion");
        col.cx = 160;
        ListView_InsertColumn(list, 2, &col);
    }

    // Linea divisoria vertical entre la lista y el detalle.
    divider_ = CreateWindowExA(0, "STATIC", "",
                               WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, kListWidth,
                               0, 1, 100, root,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDivider)),
                               inst, nullptr);

    // Detalle (derecha): nombre, categoria, descripcion y tabla de
    // propiedades (vacia de momento).
    title_ = CreateWindowExA(0, "STATIC", "Selecciona una clase",
                             WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 26,
                             root, nullptr, inst, nullptr);
    category_ = CreateWindowExA(0, "STATIC", "",
                                WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 20,
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
    ListView_SetTextColor(props, descColor());
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

    void* labels[] = {title_, category_, desc_, propsLabel_};
    for (void* h : labels) {
        SendMessageA(static_cast<HWND>(h), WM_SETFONT,
                     reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    }

    buildClassList();
    layout(rc.right, rc.bottom);
    SK_INFO("Vista de documentacion de clases lista (%d clases)", kClassCount);
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
    category_ = nullptr;
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

    for (int i = 0; i < kClassCount; ++i) {
        const ClassDef& c = kClasses[i];

        // El texto del item es el nombre (columna "Clase"); el icono y el
        // resto se pintan en paintRow. La primera columna tambien lleva el
        // nombre para que el filtrado/orden siga siendo util.
        LVITEMA item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<char*>(c.name);
        ListView_InsertItem(list, &item);
    }
    sel_ = 0;
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    updateDetails();
}

void DocsView::updateDetails() {
    if (sel_ < 0 || sel_ >= kClassCount) return;
    const ClassDef& c = kClasses[sel_];
    if (title_) {
        SetWindowTextA(static_cast<HWND>(title_), c.name);
    }
    if (category_) {
        SetWindowTextA(static_cast<HWND>(category_),
                       kCategories[c.cat].name);
    }
    if (desc_) {
        SetWindowTextA(static_cast<HWND>(desc_), c.shortDesc);
    }
    // La tabla "Propiedades" se completa en una iteracion posterior; de
    // momento queda vacia.
    if (props_) {
        ListView_DeleteAllItems(static_cast<HWND>(props_));
    }
}

void DocsView::paintRow(const DRAWITEMSTRUCT& dis) const {
    const int idx = static_cast<int>(dis.itemID);
    if (idx < 0 || idx >= kClassCount) return;
    const ClassDef& c = kClasses[idx];

    HDC hdc = dis.hDC;
    RECT rc = dis.rcItem;
    HWND list = dis.hwndItem;

    // Fondo de la fila.
    FillRect(hdc, &rc, theme::listBackgroundBrush());

    const bool selected = (dis.itemState & ODS_SELECTED) != 0;
    const bool hot = (dis.itemState & ODS_HOTLIGHT) != 0;
    if (selected || hot) {
        COLORREF fill = selected ? theme::boxBackgroundAlt() : theme::surfacePressed();
        HBRUSH b = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, selected ? theme::accent() : theme::border());
        HBRUSH ob = static_cast<HBRUSH>(SelectObject(hdc, b));
        HPEN op = static_cast<HPEN>(SelectObject(hdc, pen));
        RECT r = rc;
        r.left += 3;
        r.right -= 3;
        r.top += 1;
        r.bottom -= 1;
        RoundRect(hdc, r.left, r.top, r.right, r.bottom, 8, 8);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(pen);
        DeleteObject(b);
    }

    const int w0 = ListView_GetColumnWidth(list, 0);
    const int w1 = ListView_GetColumnWidth(list, 1);

    SetBkMode(hdc, TRANSPARENT);

    // Icono (emoji) centrado en la primera columna.
    RECT iconRc = rc;
    iconRc.right = rc.left + w0;
    wchar_t icon[8]{};
    copyW(icon, 8, kCategories[c.cat].icon);
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, emojiFont()));
    SetTextColor(hdc, nameColor());
    DrawTextW(hdc, icon, -1, &iconRc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Nombre de la clase (columna "Clase").
    SelectObject(hdc, theme::uiFont());
    RECT nameRc = rc;
    nameRc.left = rc.left + w0 + 6;
    nameRc.right = rc.left + w0 + w1;
    SetTextColor(hdc, nameColor());
    DrawTextA(hdc, c.name, -1, &nameRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX);

    // Descripcion corta (columna "Descripcion").
    RECT descRc = rc;
    descRc.left = rc.left + w0 + w1 + 6;
    descRc.right = rc.right - 6;
    SetTextColor(hdc, descColor());
    DrawTextA(hdc, c.shortDesc, -1, &descRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX);

    SelectObject(hdc, oldFont);
}

void DocsView::layout(int width, int height) {
    if (!hwnd_) return;
    const int y = WorkspaceTabs::kTopBandHeight;
    int h = height - y;
    if (h < 0) h = 0;
    MoveWindow(static_cast<HWND>(hwnd_), 0, y, width, h, TRUE);

    if (h <= 0) return;

    const int detailX = kListWidth + 14;
    const int detailW = width - detailX - 14;
    const int descH = 140;

    if (list_) {
        MoveWindow(static_cast<HWND>(list_), 0, 0, kListWidth, h, TRUE);
        // La columna "Descripcion" se estira con el ancho de la lista.
        int descW = kListWidth - kIconColWidth - kNameColWidth - 24;
        if (descW < 60) descW = 60;
        ListView_SetColumnWidth(static_cast<HWND>(list_), 2, descW);
    }
    if (divider_) {
        MoveWindow(static_cast<HWND>(divider_), kListWidth, 0, 1, h, TRUE);
    }
    if (title_) {
        MoveWindow(static_cast<HWND>(title_), detailX, 10, detailW, 26, TRUE);
    }
    if (category_) {
        MoveWindow(static_cast<HWND>(category_), detailX, 38, detailW, 20, TRUE);
    }
    if (desc_) {
        MoveWindow(static_cast<HWND>(desc_), detailX, 66, detailW, descH, TRUE);
    }
    if (propsLabel_) {
        MoveWindow(static_cast<HWND>(propsLabel_), detailX, 66 + descH + 12,
                   detailW, 20, TRUE);
    }
    if (props_) {
        const int propsY = 66 + descH + 36;
        int propsH = h - propsY - 12;
        if (propsH < 40) propsH = 40;
        MoveWindow(static_cast<HWND>(props_), detailX, propsY, detailW, propsH,
                   TRUE);
    }
}

} // namespace sk
