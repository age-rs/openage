// Copyright 2017-2024 the openage authors. See copying.md for legal info.

#include "loader.h"

#include "../../error/error.h"


namespace openage {
namespace renderer {
namespace vulkan {

VlkLoader::VlkLoader() :
	inited(false) {}

void VlkLoader::init(VkInstance instance) {
#ifndef NDEBUG
	this->pCreateDebugReportCallbackEXT = PFN_vkCreateDebugReportCallbackEXT(vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT"));
	this->pDestroyDebugReportCallbackEXT = PFN_vkDestroyDebugReportCallbackEXT(vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT"));
#endif

	this->inited = true;
}

#ifndef NDEBUG
VkResult VlkLoader::vkCreateDebugReportCallbackEXT(
	VkInstance instance,
	const VkDebugReportCallbackCreateInfoEXT *pCreateInfo,
	const VkAllocationCallbacks *pAllocator,
	VkDebugReportCallbackEXT *pCallback) {
	if (!this->inited) {
		throw Error(MSG(err) << "Tried to request function from Vulkan extension loader before initializing it.");
	}

	if (this->pCreateDebugReportCallbackEXT != nullptr) {
		return this->pCreateDebugReportCallbackEXT(instance, pCreateInfo, pAllocator, pCallback);
	}

	return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void VlkLoader::vkDestroyDebugReportCallbackEXT(
	VkInstance instance,
	VkDebugReportCallbackEXT callback,
	const VkAllocationCallbacks *pAllocator) {
	if (!this->inited) {
		throw Error(MSG(err) << "Tried to request function from Vulkan extension loader before initializing it.");
	}

	if (this->pDestroyDebugReportCallbackEXT != nullptr) {
		this->pDestroyDebugReportCallbackEXT(instance, callback, pAllocator);
	}
}
#endif

} // namespace vulkan
} // namespace renderer
} // namespace openage
