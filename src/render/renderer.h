#pragma once

#include <vulkan/vulkan.h>

#include <vector>

#include "../math/math.h"
#include "../platform/window.h"
#include "meshes.h"

namespace sk {

// Rectangulo de seleccion (marquee) en pixeles de cliente, validado
// mientras el usuario arrastra en el viewport.
struct ScreenRect {
    bool valid = false;
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

// Vertice de lineas con color (rejilla, contornos y gizmo comparten
// layout; el shader grid.vert lee pos + color por vertice).
struct LineVertex {
    float pos[3];
    float color[3];
};

// Renderer Vulkan minimo: instancia, dispositivo, swapchain y dos
// pipelines: lineas (rejilla de referencia del mundo) y triangulos (los
// objetos "Part" de la escena).
//
// Decisiones (fase 1):
//  - 2 frames en vuelo (std::vector de sync objects por frame)
//  - MVP via push constant (sin uniform buffers ni descriptor sets todavia)
//  - buffers de vertices host-visible (la rejilla y los cubos son
//    estaticos y pequenos)
class Renderer {
public:
    bool init(Window& window);
    void shutdown();

    // Devuelve false solo en error irrecuperable; los resize se resuelven
    // recreando la swapchain. objects: matriz modelo de cada Part.
    // shapes: la forma geometrica de cada Part (indice en kShapeMeshes,
    // el mismo orden que objects; si falta, se toma el cubo).
    // outlines: matriz de cada objeto seleccionado (contorno celeste,
    // ya con la escala 1.02 aplicada); marquee: rectangulo de arrastre;
    // gizmo: lineas del gizmo activo en coordenadas de mundo (se pintan
    // sin depth, por encima de la escena).
    bool drawFrame(const Mat4& viewProj, const std::vector<Mat4>& objects,
                   const std::vector<int>& shapes,
                   const std::vector<Mat4>& outlines, const ScreenRect& marquee,
                   const std::vector<LineVertex>& gizmo);

    // WM_SIZE de la ventana: llvmpipe no devuelve OUT_OF_DATE al
    // redimensionar, asi que hay que marcar la swapchain a mano.
    void invalidateSwapchain() { swapchainDirty_ = true; }

private:
    struct QueueFamilies {
        int graphics = -1;
        int present = -1;
        bool complete() const { return graphics >= 0 && present >= 0; }
    };

    struct SwapchainSupport {
        VkSurfaceCapabilitiesKHR caps{};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    bool createInstance();
    bool createSurface();
    bool pickPhysicalDevice();
    bool createDevice();
    bool createSwapchain();
    bool createImageViews();
    bool createRenderPass();
    bool createDepthResources();
    bool createFramebuffers();
    bool createPipelineLayout();
    bool createPipelineFor(VkPrimitiveTopology topology, bool depthTest,
                           VkPipeline* outPipeline);
    bool createPipelines();
    bool createGridBuffers();
    bool createShapeBuffers();     // una malla por forma (kShapeMeshes)
    bool createOutlineBuffers();  // contorno celeste + cuadrado marquee
    bool createGizmoBuffers();    // buffers por frame para las lineas
    bool createVertexBuffer(const void* vertices, size_t vertexBytes,
                            VkBuffer* outBuffer, VkDeviceMemory* outMemory);
    bool createCommandPool();
    bool createSyncObjects();
    bool recreateSwapchain();
    void destroySwapchainObjects();

    QueueFamilies findQueueFamilies(VkPhysicalDevice device) const;
    SwapchainSupport querySwapchainSupport(VkPhysicalDevice device) const;
    VkFormat findDepthFormat() const;
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
    bool loadShaderModule(const char* path, VkShaderModule* out) const;

    Window* window_ = nullptr;

    VkInstance instance_ = VK_NULL_HANDLE;
#ifdef _DEBUG
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
#endif
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages_;
    VkFormat swapchainFormat_ = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImageView> swapchainViews_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;

    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;      // lineas (rejilla)
    VkPipeline meshPipeline_ = VK_NULL_HANDLE;  // triangulos (Part)
    VkPipeline gizmoPipeline_ = VK_NULL_HANDLE; // lineas sin depth (gizmo)

    VkBuffer gridBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory gridMemory_ = VK_NULL_HANDLE;
    uint32_t gridVertexCount_ = 0;

    // Una malla por forma geometrica (la 0 es el cubo de toda la vida).
    VkBuffer shapeBuffers_[kShapeMeshes] = {};
    VkDeviceMemory shapeMemories_[kShapeMeshes] = {};
    uint32_t shapeVertexCounts_[kShapeMeshes] = {};

    static_assert(kShapeMeshes == static_cast<int>(Shape::Count),
                  "kShapeMeshes debe coincidir con Shape::Count");

    // Lineas de seleccion: cubo alambre (contorno de objeto) y cuadrado
    // en NDC para el rectangulo de arrastre.
    VkBuffer outlineBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory outlineMemory_ = VK_NULL_HANDLE;
    uint32_t outlineVertexCount_ = 0;
    VkBuffer marqueeBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory marqueeMemory_ = VK_NULL_HANDLE;
    uint32_t marqueeVertexCount_ = 0;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    static constexpr int kMaxFramesInFlight = 2;
    std::vector<VkSemaphore> imageAvailable_;
    std::vector<VkSemaphore> renderFinished_;
    std::vector<VkFence> inFlightFences_;
    int frameIndex_ = 0;

    // Lineas del gizmo: un buffer por frame en vuelo (se reescribe tras
    // esperar el fence del frame, sin barreras: el wait garantiza que el
    // frame anterior ya leyo los datos).
    static constexpr int kGizmoCapacity = 4096;  // vertices
    VkBuffer gizmoBuffers_[kMaxFramesInFlight] = {};
    VkDeviceMemory gizmoMemories_[kMaxFramesInFlight] = {};

    bool swapchainDirty_ = false;
    bool validationEnabled_ = false;
};

} // namespace sk
