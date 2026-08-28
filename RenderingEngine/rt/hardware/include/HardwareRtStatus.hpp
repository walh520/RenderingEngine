#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <utility>

namespace RenderingEngine::Rt::Hardware
{
    struct Status final
    {
        VkResult result{VK_SUCCESS};
        std::string message;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return result == VK_SUCCESS;
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return Succeeded();
        }

        [[nodiscard]] static Status Success()
        {
            return {};
        }

        [[nodiscard]] static Status Failure(VkResult resultValue, std::string messageValue)
        {
            return Status{resultValue, std::move(messageValue)};
        }
    };
}
