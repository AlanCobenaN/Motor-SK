#include "renderer.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "../core/log.h"
#include "meshes.h"

namespace sk {

namespace {

#define VK_CHECK(expr)                                \
    do {                                              \
        VkResult result_ = (expr);                    \
        if (result_ != VK_SUCCESS) {                  \
            SK_ERROR("Vulkan %s = %d", #expr, (int)result_); \
            return false;                             \
        }                                             \
    } while (0)

using Vertex = MeshVertex;

// El gizmo reutiliza el mismo layout que el resto de lineas.
using GizmoVertex = LineVertex;

// Un buffer por forma geometrica: si algun dia se anade una forma sin
// malla, el build falla aqui en lugar de leer basura en el draw.
static_assert(kShapeMeshes == static_cast<int>(Shape::Count),
              "kShapeMeshes debe coincidir con Shape::Count");

std::string executableDir() {
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string s(path);
    const size_t slash = s.find_last_of("\\/");
    return (slash == std::string::npos) ? std::string() : s.substr(0, slash + 1);
}

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    return formats.front();
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes) {
    // FIFO es el unico modo obligatorio en la spec.
    for (const auto m : modes) {
        if (m == VK_PRESENT_MODE_FIFO_KHR) return m;
    }
    return modes.front();
}

} // namespace

bool Renderer::init(Window& window) {
    window_ = &window;
    if (!createInstance()) return false;
    if (!createSurface()) return false;
    if (!pickPhysicalDevice()) return false;
    if (!createDevice()) return false;
    if (!createSwapchain()) return false;
    if (!createImageViews()) return false;
    if (!createRenderPass()) return false;
    if (!createDepthResources()) return false;
    if (!createFramebuffers()) return false;
    if (!createPipelines()) return false;
    if (!createGridBuffers()) return false;
    if (!createShapeBuffers()) return false;
    if (!createOutlineBuffers()) return false;
    if (!createGizmoBuffers()) return false;
    if (!createCommandPool()) return false;
    if (!createSyncObjects()) return false;
    SK_INFO("Renderer Vulkan listo");
    return true;
}

void Renderer::shutdown() {
    if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);

    destroySwapchainObjects();

    if (gridBuffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, gridBuffer_, nullptr);
    if (gridMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, gridMemory_, nullptr);
    for (int i = 0; i < kShapeMeshes; ++i) {
        if (shapeBuffers_[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, shapeBuffers_[i], nullptr);
        }
        if (shapeMemories_[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device_, shapeMemories_[i], nullptr);
        }
        shapeBuffers_[i] = VK_NULL_HANDLE;
        shapeMemories_[i] = VK_NULL_HANDLE;
    }
    if (outlineBuffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, outlineBuffer_, nullptr);
    if (outlineMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, outlineMemory_, nullptr);
    if (marqueeBuffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, marqueeBuffer_, nullptr);
    if (marqueeMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, marqueeMemory_, nullptr);
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        if (gizmoBuffers_[i] != VK_NULL_HANDLE) vkDestroyBuffer(device_, gizmoBuffers_[i], nullptr);
        if (gizmoMemories_[i] != VK_NULL_HANDLE) vkFreeMemory(device_, gizmoMemories_[i], nullptr);
        gizmoBuffers_[i] = VK_NULL_HANDLE;
        gizmoMemories_[i] = VK_NULL_HANDLE;
    }
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline_, nullptr);
    if (meshPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, meshPipeline_, nullptr);
    if (gizmoPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, gizmoPipeline_, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (renderPass_ != VK_NULL_HANDLE) vkDestroyRenderPass(device_, renderPass_, nullptr);

    for (size_t i = 0; i < imageAvailable_.size(); ++i) {
        vkDestroySemaphore(device_, imageAvailable_[i], nullptr);
        vkDestroySemaphore(device_, renderFinished_[i], nullptr);
        vkDestroyFence(device_, inFlightFences_[i], nullptr);
    }
    if (commandPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, commandPool_, nullptr);

    if (device_ != VK_NULL_HANDLE) vkDestroyDevice(device_, nullptr);
    if (surface_ != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance_, surface_, nullptr);

#ifdef _DEBUG
    if (debugMessenger_ != VK_NULL_HANDLE) {
        auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyMessenger) destroyMessenger(instance_, debugMessenger_, nullptr);
    }
#endif

    if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);

    device_ = VK_NULL_HANDLE;
    instance_ = VK_NULL_HANDLE;
}

bool Renderer::createInstance() {
    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "MotorSK";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "MotorSK";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    };

    std::vector<const char*> layers;
#ifdef _DEBUG
    {
        uint32_t count = 0;
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> available(count);
        vkEnumerateInstanceLayerProperties(&count, available.data());
        for (const auto& layer : available) {
            if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
                layers.push_back("VK_LAYER_KHRONOS_validation");
                validationEnabled_ = true;
                break;
            }
        }

        if (validationEnabled_) {
            uint32_t extCount = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
            std::vector<VkExtensionProperties> exts(extCount);
            vkEnumerateInstanceExtensionProperties(nullptr, &extCount, exts.data());
            for (const auto& ext : exts) {
                if (std::strcmp(ext.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) {
                    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                    break;
                }
            }
        }
    }
#endif

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &appInfo;
    ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount = static_cast<uint32_t>(layers.size());
    ci.ppEnabledLayerNames = layers.data();

    VK_CHECK(vkCreateInstance(&ci, nullptr, &instance_));

#ifdef _DEBUG
    if (validationEnabled_) {
        auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        if (createMessenger) {
            VkDebugUtilsMessengerCreateInfoEXT messengerCi{
                VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            messengerCi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            messengerCi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            messengerCi.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data,
                                             void*) -> VkBool32 {
                if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
                    SK_ERROR("[VK] %s", data->pMessage);
                } else {
                    SK_WARN("[VK] %s", data->pMessage);
                }
                return VK_FALSE;
            };
            createMessenger(instance_, &messengerCi, nullptr, &debugMessenger_);
        }
        SK_INFO("Capa de validacion activa");
    }
#endif
    return true;
}

bool Renderer::createSurface() {
    VkWin32SurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    ci.hinstance = GetModuleHandleA(nullptr);
    ci.hwnd = static_cast<HWND>(window_->nativeHandle());
    VK_CHECK(vkCreateWin32SurfaceKHR(instance_, &ci, nullptr, &surface_));
    return true;
}

Renderer::QueueFamilies Renderer::findQueueFamilies(VkPhysicalDevice device) const {
    QueueFamilies result;

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; ++i) {
        if (result.graphics < 0 && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            result.graphics = static_cast<int>(i);
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);
        if (presentSupport) {
            result.present = static_cast<int>(i);
        }

        if (result.complete()) break;
    }
    return result;
}

Renderer::SwapchainSupport Renderer::querySwapchainSupport(VkPhysicalDevice device) const {
    SwapchainSupport details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &details.caps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr);
    details.formats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, details.formats.data());

    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &modeCount, nullptr);
    details.presentModes.resize(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &modeCount, details.presentModes.data());
    return details;
}

bool Renderer::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) {
        SK_ERROR("Sin dispositivos Vulkan (no hay ICD registrado)");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    // Preferencia: discreta > integrada > CPU (lavapipe).
    auto score = [&](VkPhysicalDevice device) -> int {
        const QueueFamilies families = findQueueFamilies(device);
        if (!families.complete()) return -1;

        const SwapchainSupport support = querySwapchainSupport(device);
        if (support.formats.empty() || support.presentModes.empty()) return -1;

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);

        int s = 0;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) s = 3;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) s = 2;
        else s = 1;
        return s;
    };

    int bestScore = -1;
    for (VkPhysicalDevice device : devices) {
        const int s = score(device);
        if (s > bestScore) {
            bestScore = s;
            physicalDevice_ = device;
        }
    }

    if (physicalDevice_ == VK_NULL_HANDLE) {
        SK_ERROR("Ningun dispositivo soporta swapchain");
        return false;
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    SK_INFO("Dispositivo: %s (api %u.%u.%u)", props.deviceName,
            VK_VERSION_MAJOR(props.apiVersion),
            VK_VERSION_MINOR(props.apiVersion),
            VK_VERSION_PATCH(props.apiVersion));
    return true;
}

bool Renderer::createDevice() {
    const QueueFamilies families = findQueueFamilies(physicalDevice_);

    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    const float priority = 1.0f;

    const auto addQueue = [&](int family) {
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex = static_cast<uint32_t>(family);
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        queueInfos.push_back(qi);
    };
    addQueue(families.graphics);
    if (families.present != families.graphics) addQueue(families.present);

    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    ci.pQueueCreateInfos = queueInfos.data();
    ci.enabledExtensionCount = 1;
    ci.ppEnabledExtensionNames = extensions;

    VK_CHECK(vkCreateDevice(physicalDevice_, &ci, nullptr, &device_));

    vkGetDeviceQueue(device_, static_cast<uint32_t>(families.graphics), 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, static_cast<uint32_t>(families.present), 0, &presentQueue_);
    return true;
}

bool Renderer::createSwapchain() {
    const SwapchainSupport support = querySwapchainSupport(physicalDevice_);
    const VkSurfaceFormatKHR format = chooseSurfaceFormat(support.formats);
    const VkPresentModeKHR presentMode = choosePresentMode(support.presentModes);

    swapchainFormat_ = format.format;

    uint32_t imageCount = support.caps.minImageCount + 1;
    if (support.caps.maxImageCount > 0 && imageCount > support.caps.maxImageCount) {
        imageCount = support.caps.maxImageCount;
    }

    VkExtent2D extent = support.caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = static_cast<uint32_t>(window_->framebufferWidth());
        extent.height = static_cast<uint32_t>(window_->framebufferHeight());
    }
    swapchainExtent_ = extent;

    const QueueFamilies families = findQueueFamilies(physicalDevice_);
    uint32_t queueFamilyIndices[2] = {
        static_cast<uint32_t>(families.graphics),
        static_cast<uint32_t>(families.present),
    };

    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface_;
    ci.minImageCount = imageCount;
    ci.imageFormat = format.format;
    ci.imageColorSpace = format.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (families.graphics != families.present) {
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    ci.preTransform = support.caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = presentMode;
    ci.clipped = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_));

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &actualCount, nullptr);
    swapchainImages_.resize(actualCount);
    vkGetSwapchainImagesKHR(device_, swapchain_, &actualCount, swapchainImages_.data());
    return true;
}

bool Renderer::createImageViews() {
    swapchainViews_.resize(swapchainImages_.size());
    for (size_t i = 0; i < swapchainImages_.size(); ++i) {
        VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        ci.image = swapchainImages_[i];
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = swapchainFormat_;
        ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(device_, &ci, nullptr, &swapchainViews_[i]));
    }
    return true;
}

bool Renderer::createRenderPass() {
    VkAttachmentDescription color{};
    color.format = swapchainFormat_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    depthFormat_ = findDepthFormat();
    VkAttachmentDescription depth{};
    depth.format = depthFormat_;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    // La render pass espera a que la presentacion anterior deje de usar
    // la imagen antes de escribir, y sincroniza al final para presentar.
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    const VkAttachmentDescription attachments[2] = {color, depth};
    VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ci.attachmentCount = 2;
    ci.pAttachments = attachments;
    ci.subpassCount = 1;
    ci.pSubpasses = &subpass;
    ci.dependencyCount = 1;
    ci.pDependencies = &dependency;

    VK_CHECK(vkCreateRenderPass(device_, &ci, nullptr, &renderPass_));
    return true;
}

VkFormat Renderer::findDepthFormat() const {
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM,
    };
    for (VkFormat format : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }
    return VK_FORMAT_D32_SFLOAT;
}

uint32_t Renderer::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    SK_ERROR("No hay tipo de memoria con flags 0x%x", props);
    return UINT32_MAX;
}

bool Renderer::createDepthResources() {
    VkImageCreateInfo imageCi{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageCi.imageType = VK_IMAGE_TYPE_2D;
    imageCi.extent = {swapchainExtent_.width, swapchainExtent_.height, 1};
    imageCi.mipLevels = 1;
    imageCi.arrayLayers = 1;
    imageCi.format = depthFormat_;
    imageCi.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCi.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageCi.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCi.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VK_CHECK(vkCreateImage(device_, &imageCi, nullptr, &depthImage_));

    VkMemoryRequirements memReqs{};
    vkGetImageMemoryRequirements(device_, depthImage_, &memReqs);

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(device_, &allocInfo, nullptr, &depthMemory_));
    VK_CHECK(vkBindImageMemory(device_, depthImage_, depthMemory_, 0));

    VkImageViewCreateInfo viewCi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewCi.image = depthImage_;
    viewCi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCi.format = depthFormat_;
    viewCi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(device_, &viewCi, nullptr, &depthView_));
    return true;
}

bool Renderer::createFramebuffers() {
    framebuffers_.resize(swapchainViews_.size());
    for (size_t i = 0; i < swapchainViews_.size(); ++i) {
        const VkImageView attachments[2] = {swapchainViews_[i], depthView_};

        VkFramebufferCreateInfo ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        ci.renderPass = renderPass_;
        ci.attachmentCount = 2;
        ci.pAttachments = attachments;
        ci.width = swapchainExtent_.width;
        ci.height = swapchainExtent_.height;
        ci.layers = 1;
        VK_CHECK(vkCreateFramebuffer(device_, &ci, nullptr, &framebuffers_[i]));
    }
    return true;
}

bool Renderer::loadShaderModule(const char* path, VkShaderModule* out) const {
    const std::string fullPath = executableDir() + path;
    std::ifstream file(fullPath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        SK_ERROR("No se pudo abrir el shader: %s", fullPath.c_str());
        return false;
    }
    const size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> code(size);
    file.seekg(0);
    file.read(code.data(), static_cast<std::streamsize>(size));

    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    if (vkCreateShaderModule(device_, &ci, nullptr, out) != VK_SUCCESS) {
        SK_ERROR("vkCreateShaderModule fallo: %s", fullPath.c_str());
        return false;
    }
    return true;
}

bool Renderer::createPipelineLayout() {
    // Push constant: MVP de 4x4 float (64 bytes), compartido por los dos
    // pipelines (lineas y triangulos).
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(float) * 16;

    VkPipelineLayoutCreateInfo layoutCi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutCi.pushConstantRangeCount = 1;
    layoutCi.pPushConstantRanges = &pushRange;
    VK_CHECK(vkCreatePipelineLayout(device_, &layoutCi, nullptr, &pipelineLayout_));
    return true;
}

bool Renderer::createPipelineFor(VkPrimitiveTopology topology, bool depthTest,
                                 VkPipeline* outPipeline) {
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (!loadShaderModule("shaders/grid.spv", &vertModule)) return false;
    if (!loadShaderModule("shaders/grid.frag.spv", &fragModule)) return false;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    const VkVertexInputAttributeDescription attributes[2] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
    };

    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = topology;

    VkPipelineViewportStateCreateInfo viewport{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    const VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineCi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineCi.stageCount = 2;
    pipelineCi.pStages = stages;
    pipelineCi.pVertexInputState = &vertexInput;
    pipelineCi.pInputAssemblyState = &inputAssembly;
    pipelineCi.pViewportState = &viewport;
    pipelineCi.pRasterizationState = &raster;
    pipelineCi.pMultisampleState = &multisample;
    pipelineCi.pDepthStencilState = &depthStencil;
    pipelineCi.pColorBlendState = &blend;
    pipelineCi.pDynamicState = &dynamic;
    pipelineCi.layout = pipelineLayout_;
    pipelineCi.renderPass = renderPass_;
    pipelineCi.subpass = 0;

    const VkResult result = vkCreateGraphicsPipelines(
        device_, VK_NULL_HANDLE, 1, &pipelineCi, nullptr, outPipeline);

    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);

    if (result != VK_SUCCESS) {
        SK_ERROR("vkCreateGraphicsPipelines(topology %d) = %d",
                 (int)topology, (int)result);
        return false;
    }
    return true;
}

bool Renderer::createPipelines() {
    if (!createPipelineLayout()) return false;
    if (!createPipelineFor(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, true, &pipeline_)) {
        return false;
    }
    if (!createPipelineFor(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, &meshPipeline_)) {
        return false;
    }
    // Gizmo: lineas siempre visibles por encima de la escena.
    if (!createPipelineFor(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, false, &gizmoPipeline_)) {
        return false;
    }
    return true;
}

bool Renderer::createGridBuffers() {
    // Rejilla de 21x21 lineas (spacing 1, extencion 10) mas los ejes RGB.
    const float grey = 0.30f;
    const float minor = 0.16f;
    std::vector<Vertex> vertices;

    const auto line = [&](float x0, float y0, float z0, float x1, float y1, float z1,
                          float r, float g, float b) {
        vertices.push_back({{x0, y0, z0}, {r, g, b}});
        vertices.push_back({{x1, y1, z1}, {r, g, b}});
    };

    constexpr int kHalf = 10;
    for (int i = -kHalf; i <= kHalf; ++i) {
        const float bright = (i == 0) ? grey : minor;
        line(static_cast<float>(i), 0.0f, static_cast<float>(-kHalf),
             static_cast<float>(i), 0.0f, static_cast<float>(kHalf),
             bright, bright, bright);
        line(static_cast<float>(-kHalf), 0.0f, static_cast<float>(i),
             static_cast<float>(kHalf), 0.0f, static_cast<float>(i),
             bright, bright, bright);
    }

    // Ejes: X rojo, Y verde, Z azul (levemente elevados para no pelear
    // con el depth de la rejilla).
    line(-kHalf, 0.01f, 0.0f, kHalf, 0.01f, 0.0f, 0.85f, 0.25f, 0.25f);
    line(0.0f, 0.01f, -kHalf, 0.0f, 0.01f, kHalf, 0.30f, 0.45f, 0.95f);
    line(0.0f, 0.0f, 0.0f, 0.0f, 5.0f, 0.0f, 0.30f, 0.90f, 0.35f);

    gridVertexCount_ = static_cast<uint32_t>(vertices.size());
    return createVertexBuffer(vertices.data(), sizeof(Vertex) * vertices.size(),
                              &gridBuffer_, &gridMemory_);
}

bool Renderer::createShapeBuffers() {
    // Una malla low-poly por forma (cubo, rombo, esfera, cilindro,
    // cuna y cuna de esquina), centrada y contenida en la caja
    // unitaria: la escala la aplica la matriz modelo de cada objeto.
    std::vector<MeshVertex> vertices;
    for (int shape = 0; shape < kShapeMeshes; ++shape) {
        vertices.clear();
        buildShapeMesh(static_cast<Shape>(shape), vertices);
        if (vertices.empty()) {
            SK_ERROR("malla vacia para la forma %d", shape);
            return false;
        }
        shapeVertexCounts_[shape] = static_cast<uint32_t>(vertices.size());
        if (!createVertexBuffer(vertices.data(),
                                sizeof(MeshVertex) * vertices.size(),
                                &shapeBuffers_[shape],
                                &shapeMemories_[shape])) {
            return false;
        }
    }
    return true;
}

bool Renderer::createOutlineBuffers() {
    // Celeste de seleccion: RGB(125,210,255) en 0..1.
    constexpr float cr = 0.49f;
    constexpr float cg = 0.82f;
    constexpr float cb = 1.0f;

    // Cubo alambre de 12 aristas (24 vertices) ligeramente mas grande
    // que el cubo: el contorno asoma por el borde de la malla.
    constexpr float p = 0.5f;
    const float corners[8][3] = {
        {-p, -p, -p}, {p, -p, -p}, {p, p, -p}, {-p, p, -p},
        {-p, -p, p},  {p, -p, p},  {p, p, p},  {-p, p, p},
    };
    const int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},  // cara -Z
        {4, 5}, {5, 6}, {6, 7}, {7, 4},  // cara +Z
        {0, 4}, {1, 5}, {2, 6}, {3, 7},  // aristas de union
    };

    std::vector<Vertex> outline;
    outline.reserve(24);
    for (const auto& edge : edges) {
        for (int e = 0; e < 2; ++e) {
            const float* v = corners[edge[e]];
            outline.push_back({{v[0], v[1], v[2]}, {cr, cg, cb}});
        }
    }
    outlineVertexCount_ = static_cast<uint32_t>(outline.size());
    if (!createVertexBuffer(outline.data(), sizeof(Vertex) * outline.size(),
                            &outlineBuffer_, &outlineMemory_)) {
        return false;
    }

    // Cuadrado unitario en [-1,1] (NDC): el MVP del marquee solo lo
    // escala y centra en pixeles; z=0 para que siempre pase el depth.
    const float quad[8][3] = {
        {-1.0f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f},
        {1.0f, -1.0f, 0.0f},  {1.0f, 1.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},   {-1.0f, 1.0f, 0.0f},
        {-1.0f, 1.0f, 0.0f},  {-1.0f, -1.0f, 0.0f},
    };
    std::vector<Vertex> marquee;
    marquee.reserve(8);
    for (const float* v : quad) {
        marquee.push_back({{v[0], v[1], v[2]}, {cr, cg, cb}});
    }
    marqueeVertexCount_ = static_cast<uint32_t>(marquee.size());
    return createVertexBuffer(marquee.data(), sizeof(Vertex) * marquee.size(),
                              &marqueeBuffer_, &marqueeMemory_);
}

bool Renderer::createGizmoBuffers() {
    // Un buffer host-visible por frame en vuelo: en drawFrame se mapea,
    // se copian las lineas del frame y se desmapea tras el wait del fence.
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        if (!createVertexBuffer(nullptr, sizeof(LineVertex) * kGizmoCapacity,
                                &gizmoBuffers_[i], &gizmoMemories_[i])) {
            return false;
        }
    }
    return true;
}

bool Renderer::createVertexBuffer(const void* vertices, size_t vertexBytes,
                                  VkBuffer* outBuffer, VkDeviceMemory* outMemory) {
    VkBufferCreateInfo bufferCi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferCi.size = vertexBytes;
    bufferCi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferCi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(device_, &bufferCi, nullptr, outBuffer));

    VkMemoryRequirements memReqs{};
    vkGetBufferMemoryRequirements(device_, *outBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkAllocateMemory(device_, &allocInfo, nullptr, outMemory));
    VK_CHECK(vkBindBufferMemory(device_, *outBuffer, *outMemory, 0));

    void* data = nullptr;
    VK_CHECK(vkMapMemory(device_, *outMemory, 0, vertexBytes, 0, &data));
    if (vertices != nullptr) {
        std::memcpy(data, vertices, vertexBytes);
    }
    vkUnmapMemory(device_, *outMemory);
    return true;
}

bool Renderer::createCommandPool() {
    const QueueFamilies families = findQueueFamilies(physicalDevice_);

    VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = static_cast<uint32_t>(families.graphics);
    VK_CHECK(vkCreateCommandPool(device_, &ci, nullptr, &commandPool_));

    commandBuffers_.resize(kMaxFramesInFlight);
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
    VK_CHECK(vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data()));
    return true;
}

bool Renderer::createSyncObjects() {
    imageAvailable_.resize(kMaxFramesInFlight);
    renderFinished_.resize(kMaxFramesInFlight);
    inFlightFences_.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo semaphoreCi{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceCi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceCi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        VK_CHECK(vkCreateSemaphore(device_, &semaphoreCi, nullptr, &imageAvailable_[i]));
        VK_CHECK(vkCreateSemaphore(device_, &semaphoreCi, nullptr, &renderFinished_[i]));
        VK_CHECK(vkCreateFence(device_, &fenceCi, nullptr, &inFlightFences_[i]));
    }
    return true;
}

void Renderer::destroySwapchainObjects() {
    for (VkFramebuffer framebuffer : framebuffers_) {
        vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }
    framebuffers_.clear();

    if (depthView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, depthView_, nullptr);
        depthView_ = VK_NULL_HANDLE;
    }
    if (depthImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, depthImage_, nullptr);
        depthImage_ = VK_NULL_HANDLE;
    }
    if (depthMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, depthMemory_, nullptr);
        depthMemory_ = VK_NULL_HANDLE;
    }

    for (VkImageView view : swapchainViews_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    swapchainViews_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

bool Renderer::recreateSwapchain() {
    // Ventana minimizada: esperar a que tenga tamano real.
    if (window_->framebufferWidth() == 0 || window_->framebufferHeight() == 0) {
        return true;
    }

    vkDeviceWaitIdle(device_);
    destroySwapchainObjects();

    if (!createSwapchain()) return false;
    if (!createImageViews()) return false;
    if (!createDepthResources()) return false;
    if (!createFramebuffers()) return false;

    swapchainDirty_ = false;
    return true;
}

bool Renderer::drawFrame(const Mat4& viewProj, const std::vector<Mat4>& objects,
                         const std::vector<int>& shapes,
                         const std::vector<Mat4>& outlines,
                         const ScreenRect& marquee,
                         const std::vector<LineVertex>& gizmo) {
    if (swapchainDirty_ && !recreateSwapchain()) return false;
    if (swapchain_ == VK_NULL_HANDLE) return true; // ventana minimizada

    const int frame = frameIndex_;

    VK_CHECK(vkWaitForFences(device_, 1, &inFlightFences_[frame], VK_TRUE, UINT64_MAX));

    // El fence garantiza que la ultima vez que este frame corrio ya leyo
    // su buffer: se puede reescribir sin barreras ni transiciones.
    if (!gizmo.empty() && gizmoBuffers_[frame] != VK_NULL_HANDLE) {
        const uint32_t count = std::min<uint32_t>(
            static_cast<uint32_t>(gizmo.size()),
            static_cast<uint32_t>(kGizmoCapacity));
        void* data = nullptr;
        if (vkMapMemory(device_, gizmoMemories_[frame], 0,
                        sizeof(LineVertex) * count, 0, &data) == VK_SUCCESS) {
            std::memcpy(data, gizmo.data(), sizeof(LineVertex) * count);
            vkUnmapMemory(device_, gizmoMemories_[frame]);
        }
    }

    uint32_t imageIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(
        device_, swapchain_, UINT64_MAX, imageAvailable_[frame], VK_NULL_HANDLE, &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchainDirty_ = true;
        return true;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        SK_ERROR("vkAcquireNextImageKHR = %d", (int)acquireResult);
        return false;
    }

    VK_CHECK(vkResetFences(device_, 1, &inFlightFences_[frame]));
    VK_CHECK(vkResetCommandBuffer(commandBuffers_[frame], 0));

    VkCommandBuffer command = commandBuffers_[frame];
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(command, &beginInfo));

    const VkClearValue clearValues[2] = {
        {{0.055f, 0.06f, 0.075f, 1.0f}},
        {{1.0f, 0}},
    };

    VkRenderPassBeginInfo renderPassBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassBegin.renderPass = renderPass_;
    renderPassBegin.framebuffer = framebuffers_[imageIndex];
    renderPassBegin.renderArea.offset = {0, 0};
    renderPassBegin.renderArea.extent = swapchainExtent_;
    renderPassBegin.clearValueCount = 2;
    renderPassBegin.pClearValues = clearValues;

    vkCmdBeginRenderPass(command, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);

    const VkViewport viewport{
        0.0f, 0.0f,
        static_cast<float>(swapchainExtent_.width),
        static_cast<float>(swapchainExtent_.height),
        0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, swapchainExtent_};

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);

    const VkBuffer vertexBuffer = gridBuffer_;
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &vertexBuffer, &offset);

    vkCmdPushConstants(command, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(float) * 16, &viewProj);
    vkCmdDraw(command, gridVertexCount_, 1, 0, 0);

    // Objetos "Part": mismo shader y push constant, pero la matriz ya
    // viene multiplicada (viewProj * model) y el pipeline es de
    // triangulos. Se recorre por forma para bindear cada malla una
    // sola vez (seis formas como mucho); un objeto con una forma
    // desconocida o una lista shapes mas corta pinta como cubo.
    if (!objects.empty() && meshPipeline_ != VK_NULL_HANDLE) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline_);
        const VkDeviceSize meshOffset = 0;

        for (int shape = 0; shape < kShapeMeshes; ++shape) {
            bool bound = false;
            for (size_t i = 0; i < objects.size(); ++i) {
                int objShape = (i < shapes.size()) ? shapes[i] : 0;
                if (objShape < 0 || objShape >= kShapeMeshes) objShape = 0;
                if (objShape != shape) continue;
                if (!bound) {
                    vkCmdBindVertexBuffers(command, 0, 1, &shapeBuffers_[shape],
                                           &meshOffset);
                    bound = true;
                }
                const Mat4 mvp = viewProj * objects[i];
                vkCmdPushConstants(command, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(float) * 16, &mvp);
                vkCmdDraw(command, shapeVertexCounts_[shape], 1, 0, 0);
            }
        }
    }

    // Contorno celeste de los objetos seleccionados (lineas).
    if (!outlines.empty() && outlineBuffer_ != VK_NULL_HANDLE) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        const VkBuffer outlineBuffer = outlineBuffer_;
        const VkDeviceSize outlineOffset = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &outlineBuffer, &outlineOffset);

        for (const Mat4& model : outlines) {
            const Mat4 mvp = viewProj * model;
            vkCmdPushConstants(command, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(float) * 16, &mvp);
            vkCmdDraw(command, outlineVertexCount_, 1, 0, 0);
        }
    }

    // Gizmo (mover/escalar/rotar): lineas de mundo sin depth, siempre
    // encima de la escena y del contorno de seleccion.
    if (!gizmo.empty() && gizmoPipeline_ != VK_NULL_HANDLE) {
        const uint32_t gizmoCount = std::min<uint32_t>(
            static_cast<uint32_t>(gizmo.size()),
            static_cast<uint32_t>(kGizmoCapacity));
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, gizmoPipeline_);
        const VkBuffer gizmoBuffer = gizmoBuffers_[frame];
        const VkDeviceSize gizmoOffset = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &gizmoBuffer, &gizmoOffset);
        vkCmdPushConstants(command, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(float) * 16, &viewProj);
        vkCmdDraw(command, gizmoCount, 1, 0, 0);
    }

    // Rectangulo de arrastre (marquee): coordenadas de cliente a NDC.
    // En Vulkan el clip Y crece hacia abajo (igual que los pixeles), asi
    // que basta mapear [0,h] a [-1,1] sin invertir nada.
    if (marquee.valid && marqueeBuffer_ != VK_NULL_HANDLE &&
        pipeline_ != VK_NULL_HANDLE) {
        const float w = static_cast<float>(swapchainExtent_.width);
        const float h = static_cast<float>(swapchainExtent_.height);
        const Vec3 center{((marquee.x0 + marquee.x1) * 0.5f) / w * 2.0f - 1.0f,
                          ((marquee.y0 + marquee.y1) * 0.5f) / h * 2.0f - 1.0f,
                          0.0f};
        // Media extension en NDC: [0,w] ocupa [-1,1], asi que la mitad
        // del rectangulo es (x1-x0)/w. La formula anterior llevaba un
        // 0.5 de mas y el rect salia a la mitad de tamano (las esquinas
        // no seguian al puntero).
        const Vec3 half{(marquee.x1 - marquee.x0) / w,
                        (marquee.y1 - marquee.y0) / h, 1.0f};
        const Mat4 mvp = translate(center) * sk::scale(half);

        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline_);
        const VkBuffer marqueeBuffer = marqueeBuffer_;
        const VkDeviceSize marqueeOffset = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &marqueeBuffer, &marqueeOffset);
        vkCmdPushConstants(command, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(float) * 16, &mvp);
        vkCmdDraw(command, marqueeVertexCount_, 1, 0, 0);
    }

    vkCmdEndRenderPass(command);
    VK_CHECK(vkEndCommandBuffer(command));

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailable_[frame];
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &command;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinished_[frame];

    VK_CHECK(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFences_[frame]));

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished_[frame];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;

    const VkResult presentResult = vkQueuePresentKHR(presentQueue_, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        swapchainDirty_ = true;
    } else if (presentResult != VK_SUCCESS) {
        SK_ERROR("vkQueuePresentKHR = %d", (int)presentResult);
        return false;
    }

    frameIndex_ = (frameIndex_ + 1) % kMaxFramesInFlight;
    return true;
}

} // namespace sk
