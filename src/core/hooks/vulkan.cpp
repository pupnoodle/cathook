/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/hooks/vulkan.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include <vulkan/vulkan.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>
#include "core/print.hpp"
#include "core/detach.hpp"
#include "core/ui/mono_ui.hpp"
#include "features/automation/nographics/nographics.hpp"
#include "features/menu/menu.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_vulkan.h"

VkResult (*queue_present_original)(VkQueue, const VkPresentInfoKHR*) = nullptr;
PFN_vkCreateDevice create_device_original = nullptr;
PFN_vkGetDeviceQueue get_device_queue_original = nullptr;
PFN_vkGetDeviceQueue2 get_device_queue2_original = nullptr;
PFN_vkCreateSwapchainKHR create_swapchain_original = nullptr;
PFN_vkDestroySwapchainKHR destroy_swapchain_original = nullptr;
PFN_vkAcquireNextImageKHR acquire_next_image_original = nullptr;
PFN_vkAcquireNextImage2KHR acquire_next_image2_original = nullptr;
PFN_vkDestroyDevice destroy_device_original = nullptr;

namespace {

struct vk_device_state {
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  std::vector<std::uint32_t> queue_families{};
  VkCommandPool command_pool = VK_NULL_HANDLE;
  std::uint32_t command_pool_family = ~0u;
};

struct vk_swapchain_state {
  VkDevice device = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent{};
  std::uint32_t min_image_count = 0;
  bool resources_ready = false;
  VkRenderPass render_pass = VK_NULL_HANDLE;
  VkCommandPool command_pool = VK_NULL_HANDLE;
  std::vector<VkImage> images{};
  std::vector<VkImageView> image_views{};
  std::vector<VkFramebuffer> framebuffers{};
  std::vector<VkCommandBuffer> command_buffers{};
  std::vector<VkSemaphore> present_semaphores{};
};

std::mutex vk_state_mutex;
std::unordered_map<VkDevice, vk_device_state> vk_devices{};
std::unordered_map<VkQueue, std::pair<VkDevice, std::uint32_t>> vk_queues{};
std::unordered_map<VkSwapchainKHR, vk_swapchain_state> vk_swapchains{};
VkInstance vk_overlay_instance = VK_NULL_HANDLE;
VkPhysicalDevice vk_overlay_physical_device = VK_NULL_HANDLE;
VkDevice vk_imgui_device = VK_NULL_HANDLE;
bool vk_imgui_initialized = false;
std::atomic_bool vk_overlay_disabled = false;
bool vk_logged_missing_device = false;
bool vk_logged_missing_swapchain = false;
bool vk_logged_missing_window = false;

void free_swapchain_resources(const VkDevice device, vk_swapchain_state &state) {
  if (device != VK_NULL_HANDLE && state.resources_ready) {
    for (const VkImageView view : state.image_views) {
      if (view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, view, nullptr);
      }
    }
    for (const VkFramebuffer framebuffer : state.framebuffers) {
      if (framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
      }
    }
    for (const VkSemaphore semaphore : state.present_semaphores) {
      if (semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, semaphore, nullptr);
      }
    }
    if (!state.command_buffers.empty() && state.command_pool != VK_NULL_HANDLE) {
      vkFreeCommandBuffers(device, state.command_pool,
        static_cast<std::uint32_t>(state.command_buffers.size()), state.command_buffers.data());
    }
    if (state.render_pass != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device, state.render_pass, nullptr);
    }
  }

  state.resources_ready = false;
  state.render_pass = VK_NULL_HANDLE;
  state.command_pool = VK_NULL_HANDLE;
  state.images.clear();
  state.image_views.clear();
  state.framebuffers.clear();
  state.command_buffers.clear();
  state.present_semaphores.clear();
}

VkCommandPool ensure_command_pool(const VkDevice device, const std::uint32_t family) {
  auto &device_state = vk_devices[device];
  if (device_state.command_pool != VK_NULL_HANDLE && device_state.command_pool_family != family) {
    vkDestroyCommandPool(device, device_state.command_pool, nullptr);
    device_state.command_pool = VK_NULL_HANDLE;
  }
  if (device_state.command_pool == VK_NULL_HANDLE) {
    VkCommandPoolCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    create_info.queueFamilyIndex = family;
    if (vkCreateCommandPool(device, &create_info, nullptr, &device_state.command_pool) != VK_SUCCESS) {
      return VK_NULL_HANDLE;
    }
    device_state.command_pool_family = family;
  }
  return device_state.command_pool;
}

bool ensure_swapchain_resources(const VkSwapchainKHR swapchain, vk_swapchain_state &state,
    const std::uint32_t family) {
  if (state.resources_ready) {
    return true;
  }
  if (state.device == VK_NULL_HANDLE || state.format == VK_FORMAT_UNDEFINED) {
    return false;
  }

  std::uint32_t image_count = 0;
  if (vkGetSwapchainImagesKHR(state.device, swapchain, &image_count, nullptr) != VK_SUCCESS ||
      image_count < 2 || image_count > 8) {
    return false;
  }
  state.images.resize(image_count);
  if (vkGetSwapchainImagesKHR(state.device, swapchain, &image_count, state.images.data()) != VK_SUCCESS) {
    return false;
  }
  state.images.resize(image_count);

  VkAttachmentDescription attachment{};
  attachment.format = state.format;
  attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  const VkAttachmentReference color_reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_reference;

  const VkSubpassDependency dependencies[2] = {
    {
      VK_SUBPASS_EXTERNAL,
      0,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      0,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      0,
    },
    {
      0,
      VK_SUBPASS_EXTERNAL,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      0,
      0,
    },
  };

  VkRenderPassCreateInfo render_pass_info{};
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_info.attachmentCount = 1;
  render_pass_info.pAttachments = &attachment;
  render_pass_info.subpassCount = 1;
  render_pass_info.pSubpasses = &subpass;
  render_pass_info.dependencyCount = 2;
  render_pass_info.pDependencies = dependencies;

  if (vkCreateRenderPass(state.device, &render_pass_info, nullptr, &state.render_pass) != VK_SUCCESS) {
    return false;
  }

  state.image_views.resize(image_count, VK_NULL_HANDLE);
  state.framebuffers.resize(image_count, VK_NULL_HANDLE);
  state.present_semaphores.resize(image_count, VK_NULL_HANDLE);

  for (std::uint32_t i = 0; i < image_count; ++i) {
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = state.images[i];
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = state.format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    if (vkCreateImageView(state.device, &view_info, nullptr, &state.image_views[i]) != VK_SUCCESS) {
      free_swapchain_resources(state.device, state);
      return false;
    }

    VkFramebufferCreateInfo framebuffer_info{};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = state.render_pass;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = &state.image_views[i];
    framebuffer_info.width = state.extent.width;
    framebuffer_info.height = state.extent.height;
    framebuffer_info.layers = 1;
    if (vkCreateFramebuffer(state.device, &framebuffer_info, nullptr, &state.framebuffers[i]) != VK_SUCCESS) {
      free_swapchain_resources(state.device, state);
      return false;
    }

    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(state.device, &semaphore_info, nullptr, &state.present_semaphores[i]) != VK_SUCCESS) {
      free_swapchain_resources(state.device, state);
      return false;
    }
  }

  const VkCommandPool pool = ensure_command_pool(state.device, family);
  if (pool == VK_NULL_HANDLE) {
    free_swapchain_resources(state.device, state);
    return false;
  }

  state.command_buffers.resize(image_count, VK_NULL_HANDLE);
  VkCommandBufferAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocate_info.commandPool = pool;
  allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocate_info.commandBufferCount = image_count;
  if (vkAllocateCommandBuffers(state.device, &allocate_info, state.command_buffers.data()) != VK_SUCCESS) {
    state.command_buffers.clear();
    free_swapchain_resources(state.device, state);
    return false;
  }
  state.command_pool = pool;

  state.resources_ready = true;
  return true;
}

VkDevice device_for_queue_locked(const VkQueue queue) {
  const auto queue_it = vk_queues.find(queue);
  if (queue_it != vk_queues.end()) {
    return queue_it->second.first;
  }
  if (vk_devices.size() == 1) {
    return vk_devices.begin()->first;
  }
  const void *const queue_dispatch = *reinterpret_cast<void *const *>(queue);
  for (const auto &[device, state] : vk_devices) {
    (void)state;
    if (*reinterpret_cast<void *const *>(device) == queue_dispatch ||
        queue_dispatch == static_cast<const void *>(device)) {
      return device;
    }
  }
  return VK_NULL_HANDLE;
}

std::uint32_t family_for_queue_locked(const VkDevice device, const VkQueue queue) {
  const auto queue_it = vk_queues.find(queue);
  if (queue_it != vk_queues.end()) {
    return queue_it->second.second;
  }
  const auto device_it = vk_devices.find(device);
  if (device_it == vk_devices.end() || device_it->second.queue_families.empty()) {
    return ~0u;
  }
  const auto &families = device_it->second.queue_families;
  if (families.size() == 1) {
    return families.front();
  }
  if (vk_overlay_physical_device != VK_NULL_HANDLE) {
    std::uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vk_overlay_physical_device, &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(vk_overlay_physical_device, &family_count, properties.data());
    for (const std::uint32_t family : families) {
      if (family < family_count && (properties[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
        return family;
      }
    }
  }
  return families.front();
}

SDL_Window *find_vulkan_sdl_window() {
  for (unsigned int id = 1; id <= 128; ++id) {
    SDL_Window *const window = SDL_GetWindowFromID(id);
    if (window == nullptr) {
      continue;
    }
    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_VULKAN) != 0) {
      return window;
    }
  }
  return nullptr;
}

bool try_initialize_overlay_locked(const VkDevice device, const VkQueue queue, const std::uint32_t family) {
  if (vk_imgui_initialized || vk_overlay_disabled) {
    return vk_imgui_initialized;
  }
  if (device == VK_NULL_HANDLE || queue == VK_NULL_HANDLE || family == ~0u) {
    return false;
  }

  if (sdl_window == nullptr) {
    sdl_window = find_vulkan_sdl_window();
  }
  if (sdl_window == nullptr) {
    if (!vk_logged_missing_window) {
      vk_logged_missing_window = true;
      print("[renderer] vulkan overlay: SDL window not found yet\n");
    }
    return false;
  }

  VkSwapchainKHR target_handle = VK_NULL_HANDLE;
  vk_swapchain_state *target = nullptr;
  for (auto &[handle, state] : vk_swapchains) {
    if (state.device == device && state.format != VK_FORMAT_UNDEFINED) {
      target_handle = handle;
      target = &state;
      break;
    }
  }
  if (target == nullptr) {
    if (!vk_logged_missing_swapchain) {
      vk_logged_missing_swapchain = true;
      print("[renderer] vulkan overlay: no captured swapchain state; overlay unavailable\n");
    }
    return false;
  }
  if (!ensure_swapchain_resources(target_handle, *target, family)) {
    return false;
  }

  VkPhysicalDevice physical_device = vk_devices[device].physical_device;
  if (physical_device == VK_NULL_HANDLE) {
    physical_device = vk_overlay_physical_device;
  }
  if (physical_device == VK_NULL_HANDLE || vk_overlay_instance == VK_NULL_HANDLE) {
    return false;
  }

  ImGui_ImplVulkan_InitInfo init_info{};
  init_info.Instance = vk_overlay_instance;
  init_info.PhysicalDevice = physical_device;
  init_info.Device = device;
  init_info.QueueFamily = family;
  init_info.Queue = queue;
  init_info.DescriptorPool = VK_NULL_HANDLE;
  init_info.DescriptorPoolSize = 32;
  init_info.MinImageCount = std::max(2u, std::min(target->min_image_count,
    static_cast<std::uint32_t>(target->images.size())));
  init_info.ImageCount = static_cast<std::uint32_t>(target->images.size());
  init_info.RenderPass = target->render_pass;
  init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

  vk_imgui_initialized = mono_ui_initialize_vulkan(sdl_window, init_info);
  if (vk_imgui_initialized) {
    vk_imgui_device = device;
    print("[renderer] vulkan overlay active (device=%p queue=%p family=%u format=%d images=%u)\n",
      device, queue, family, static_cast<int>(target->format), init_info.ImageCount);
  }
  return vk_imgui_initialized;
}

bool render_overlay_into_swapchain(vk_swapchain_state &state, const std::uint32_t image_index,
    const VkQueue queue, const VkSemaphore wait_semaphore, VkSemaphore *const out_signal) {
  if (image_index >= state.images.size()) {
    return false;
  }

  const VkCommandBuffer command_buffer = state.command_buffers[image_index];
  const VkSemaphore signal_semaphore = state.present_semaphores[image_index];

  vkResetCommandBuffer(command_buffer, 0);

  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
    return false;
  }

  VkRenderPassBeginInfo render_pass_begin{};
  render_pass_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_pass_begin.renderPass = state.render_pass;
  render_pass_begin.framebuffer = state.framebuffers[image_index];
  render_pass_begin.renderArea.extent = state.extent;
  vkCmdBeginRenderPass(command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);
  mono_ui_vulkan_render(command_buffer);
  vkCmdEndRenderPass(command_buffer);

  if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
    return false;
  }

  const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = &wait_semaphore;
  submit_info.pWaitDstStageMask = &wait_stage;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer;
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = &signal_semaphore;

  if (vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS) {
    return false;
  }

  *out_signal = signal_semaphore;
  return true;
}

}

void vulkan_overlay_set_instance(const VkInstance instance, const VkPhysicalDevice physical_device) {
  vk_overlay_instance = instance;
  vk_overlay_physical_device = physical_device;
}

void mono_ui_vulkan_device_idle() {
  std::lock_guard<std::mutex> lock(vk_state_mutex);
  if (vk_imgui_device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(vk_imgui_device);
  }
}

void mono_ui_vulkan_resources_shutdown(const bool release_graphics_resources) {
  std::lock_guard<std::mutex> lock(vk_state_mutex);
  if (!release_graphics_resources) {
    vk_devices.clear();
    vk_queues.clear();
    vk_swapchains.clear();
    vk_imgui_device = VK_NULL_HANDLE;
    vk_imgui_initialized = false;
    vk_overlay_disabled = false;
    return;
  }

  for (auto &[device, state] : vk_devices) {
    vkDeviceWaitIdle(device);
  }
  for (auto &[handle, state] : vk_swapchains) {
    free_swapchain_resources(state.device, state);
  }
  for (auto &[device, state] : vk_devices) {
    if (state.command_pool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device, state.command_pool, nullptr);
      state.command_pool = VK_NULL_HANDLE;
    }
  }
  vk_devices.clear();
  vk_queues.clear();
  vk_swapchains.clear();
  vk_imgui_device = VK_NULL_HANDLE;
  vk_imgui_initialized = false;
  vk_overlay_disabled = false;
  vk_logged_missing_device = false;
  vk_logged_missing_swapchain = false;
  vk_logged_missing_window = false;

  if (vk_overlay_instance != VK_NULL_HANDLE) {
    vkDestroyInstance(vk_overlay_instance, nullptr);
    vk_overlay_instance = VK_NULL_HANDLE;
    vk_overlay_physical_device = VK_NULL_HANDLE;
  }
}

VkResult create_device_hook(const VkPhysicalDevice physical_device, const VkDeviceCreateInfo *create_info,
    const VkAllocationCallbacks *allocator, VkDevice *out_device) {
  CATHOOK_HOOK_GUARD();
  if (create_device_original == nullptr) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  const VkResult result = create_device_original(physical_device, create_info, allocator, out_device);
  if (result == VK_SUCCESS && out_device != nullptr && *out_device != VK_NULL_HANDLE &&
      create_info != nullptr) {
    std::lock_guard<std::mutex> lock(vk_state_mutex);
    auto &device_state = vk_devices[*out_device];
    device_state.physical_device = physical_device;
    device_state.queue_families.clear();
    if (create_info->pQueueCreateInfos != nullptr) {
      for (std::uint32_t i = 0; i < create_info->queueCreateInfoCount; ++i) {
        device_state.queue_families.push_back(create_info->pQueueCreateInfos[i].queueFamilyIndex);
      }
    }
  }
  return result;
}

void get_device_queue_hook(const VkDevice device, const std::uint32_t family, const std::uint32_t index,
    VkQueue *out_queue) {
  CATHOOK_HOOK_GUARD();
  if (get_device_queue_original == nullptr) {
    return;
  }
  get_device_queue_original(device, family, index, out_queue);
  if (out_queue != nullptr && *out_queue != VK_NULL_HANDLE) {
    std::lock_guard<std::mutex> lock(vk_state_mutex);
    vk_queues[*out_queue] = {device, family};
  }
}

void get_device_queue2_hook(const VkDevice device, const VkDeviceQueueInfo2 *queue_info,
    VkQueue *out_queue) {
  CATHOOK_HOOK_GUARD();
  if (get_device_queue2_original == nullptr) {
    return;
  }
  get_device_queue2_original(device, queue_info, out_queue);
  if (queue_info != nullptr && out_queue != nullptr && *out_queue != VK_NULL_HANDLE) {
    std::lock_guard<std::mutex> lock(vk_state_mutex);
    vk_queues[*out_queue] = {device, queue_info->queueFamilyIndex};
  }
}

VkResult create_swapchain_hook(const VkDevice device, const VkSwapchainCreateInfoKHR *create_info,
    const VkAllocationCallbacks *allocator, VkSwapchainKHR *out_swapchain) {
  CATHOOK_HOOK_GUARD();
  if (create_swapchain_original == nullptr) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  const VkResult result = create_swapchain_original(device, create_info, allocator, out_swapchain);
  if (result == VK_SUCCESS && create_info != nullptr && out_swapchain != nullptr &&
      *out_swapchain != VK_NULL_HANDLE) {
    std::lock_guard<std::mutex> lock(vk_state_mutex);
    vk_devices[device];
    auto &state = vk_swapchains[*out_swapchain];
    state.device = device;
    state.surface = create_info->surface;
    state.format = create_info->imageFormat;
    state.extent = create_info->imageExtent;
    state.min_image_count = create_info->minImageCount;
    state.resources_ready = false;
  }
  return result;
}

void destroy_swapchain_hook(const VkDevice device, const VkSwapchainKHR swapchain,
    const VkAllocationCallbacks *allocator) {
  CATHOOK_HOOK_GUARD();
  if (destroy_swapchain_original == nullptr) {
    return;
  }
  destroy_swapchain_original(device, swapchain, allocator);
  std::lock_guard<std::mutex> lock(vk_state_mutex);
  const auto it = vk_swapchains.find(swapchain);
  if (it != vk_swapchains.end()) {
    free_swapchain_resources(device, it->second);
    vk_swapchains.erase(it);
  }
}

VkResult acquire_next_image_hook(const VkDevice device, const VkSwapchainKHR swapchain,
    const std::uint64_t timeout, const VkSemaphore semaphore, const VkFence fence,
    std::uint32_t *out_index) {
  CATHOOK_HOOK_GUARD();
  if (acquire_next_image_original == nullptr) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }
  const VkResult result =
      acquire_next_image_original(device, swapchain, timeout, semaphore, fence, out_index);
  {
    std::lock_guard<std::mutex> lock(vk_state_mutex);
    vk_devices[device];
    auto &state = vk_swapchains[swapchain];
    if (state.device == VK_NULL_HANDLE) {
      state.device = device;
    }
  }
  return result;
}

VkResult acquire_next_image2_hook(const VkDevice device, const VkAcquireNextImageInfoKHR *acquire_info,
    std::uint32_t *out_index) {
  CATHOOK_HOOK_GUARD();
  if (acquire_next_image2_original == nullptr) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }
  const VkResult result = acquire_next_image2_original(device, acquire_info, out_index);
  if (acquire_info != nullptr) {
    std::lock_guard<std::mutex> lock(vk_state_mutex);
    vk_devices[device];
    auto &state = vk_swapchains[acquire_info->swapchain];
    if (state.device == VK_NULL_HANDLE) {
      state.device = device;
    }
  }
  return result;
}

void destroy_device_hook(const VkDevice device, const VkAllocationCallbacks *allocator) {
  CATHOOK_HOOK_GUARD();
  {
    std::lock_guard<std::mutex> lock(vk_state_mutex);
    for (auto it = vk_swapchains.begin(); it != vk_swapchains.end();) {
      if (it->second.device == device) {
        free_swapchain_resources(device, it->second);
        it = vk_swapchains.erase(it);
      } else {
        ++it;
      }
    }
    for (auto it = vk_queues.begin(); it != vk_queues.end();) {
      if (it->second.first == device) {
        it = vk_queues.erase(it);
      } else {
        ++it;
      }
    }
    const auto device_it = vk_devices.find(device);
    if (device_it != vk_devices.end()) {
      if (device_it->second.command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, device_it->second.command_pool, nullptr);
      }
      vk_devices.erase(device_it);
    }
    if (device == vk_imgui_device) {
      mono_ui_vulkan_mark_device_lost();
      vk_imgui_device = VK_NULL_HANDLE;
    }
  }
  if (destroy_device_original != nullptr) {
    destroy_device_original(device, allocator);
  }
}

VkResult queue_present_hook(VkQueue queue, const VkPresentInfoKHR* present_info)
{
  CATHOOK_HOOK_GUARD();
  if (queue_present_original == nullptr) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  VkPresentInfoKHR modified_info{};
  std::vector<VkSemaphore> signal_semaphores{};
  std::vector<std::pair<vk_swapchain_state*, std::uint32_t>> used_slots{};
  bool overlay_applied = false;

  {
    mono_ui_lock();
    std::lock_guard<std::mutex> vk_lock(vk_state_mutex);

    const bool can_overlay = present_info != nullptr && present_info->swapchainCount > 0 &&
        present_info->waitSemaphoreCount >= present_info->swapchainCount &&
        !cathook::core::is_detach_pending() &&
        !nographics::should_skip_rendering_hooks() &&
        !vk_overlay_disabled.load(std::memory_order_acquire);

    if (can_overlay) {
      const VkDevice device = device_for_queue_locked(queue);
      const std::uint32_t family =
          device != VK_NULL_HANDLE ? family_for_queue_locked(device, queue) : ~0u;
      if (device == VK_NULL_HANDLE || family == ~0u) {
        if (!vk_logged_missing_device) {
          vk_logged_missing_device = true;
          print("[renderer] vulkan overlay: presenting queue not attributed to a captured device\n");
        }
      } else {
        if (!vk_imgui_initialized) {
          try_initialize_overlay_locked(device, queue, family);
        }
        if (vk_imgui_initialized && device == vk_imgui_device && mono_ui_vulkan_frame_pending()) {
          signal_semaphores.resize(present_info->swapchainCount, VK_NULL_HANDLE);
          overlay_applied = true;
          for (std::uint32_t i = 0; i < present_info->swapchainCount; ++i) {
            const auto swapchain_it = vk_swapchains.find(present_info->pSwapchains[i]);
            if (swapchain_it == vk_swapchains.end() ||
                !ensure_swapchain_resources(present_info->pSwapchains[i], swapchain_it->second, family) ||
                !mono_ui_vulkan_prepare_target(swapchain_it->second.render_pass,
                    swapchain_it->second.min_image_count,
                    static_cast<std::uint32_t>(swapchain_it->second.images.size())) ||
                present_info->pImageIndices == nullptr ||
                !render_overlay_into_swapchain(swapchain_it->second, present_info->pImageIndices[i],
                    queue, present_info->pWaitSemaphores[i], &signal_semaphores[i])) {
              overlay_applied = false;
              break;
            }
            used_slots.emplace_back(&swapchain_it->second, present_info->pImageIndices[i]);
          }
          if (overlay_applied) {
            mono_ui_vulkan_frame_consumed();
          }
        }
      }
    }

    if (overlay_applied) {
      modified_info = *present_info;
      modified_info.pWaitSemaphores = signal_semaphores.data();
      present_info = &modified_info;
    }

    const VkResult result = queue_present_original(queue, present_info);

    if (overlay_applied && result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
      for (const auto &[state, image_index] : used_slots) {
        if (image_index >= state->present_semaphores.size()) {
          continue;
        }
        VkSemaphore &semaphore = state->present_semaphores[image_index];
        if (semaphore != VK_NULL_HANDLE) {
          vkDestroySemaphore(state->device, semaphore, nullptr);
          semaphore = VK_NULL_HANDLE;
        }
        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkCreateSemaphore(state->device, &semaphore_info, nullptr, &semaphore);
      }
    }

    mono_ui_unlock();
    cathook::core::service_detach_request();
    return result;
  }
}
