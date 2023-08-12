#include "pch.h"

int main()
{
	glfwInit();

	// Define some constants to be used throughout the program:
	const int WIDTH = 800;  // Leave at 800x800, please!
	const int HEIGHT = 800;
	const uint32_t CONCURRENT_FRAMES = 3; // How many frames do we want our swapchain to have/to render in parallel

	// A storage for resource cleanup handlers (will be invoked at the end of the application):
	std::list<std::function<void()>> cleanupHandlers;

	// ===> 1. Create a window
	auto window = helpers::create_window_with_glfw(WIDTH, HEIGHT);
	// ===> 2. Create a vulkan instance
	auto vkInst = helpers::create_vulkan_instance_with_validation_layers();
	// ===> 3. Create a surface to draw into
	auto surface = helpers::create_surface(window, vkInst);
	// ===> 4. Get a handle to (one) physical device, i.e. to the GPU
	auto physicalDevice = vkInst.enumeratePhysicalDevices().front();
	std::cout << "selected physical device: " << physicalDevice.getProperties().deviceName << std::endl;
	// ===> 5. Create a logical device which serves as this application's interface to the physical device
	auto device = helpers::create_logical_device(physicalDevice, surface);
	// ===> 6. Get a queue on the logical device so we can send commands to it
	auto [queueFamilyIndex, queue] = helpers::get_queue_on_logical_device(physicalDevice, surface, device);

	// ------------------------------------------------------------------------------
	// Task from Part 1: Query physical device for supported formats and select one!
	// Task from Part 1: Query physical device for supported presentation modes and select one!
	// 
	auto availableFormats = physicalDevice.getSurfaceFormatsKHR(surface);
	auto availablePresentationModes = physicalDevice.getSurfacePresentModesKHR(surface);
	auto selectedFormatAndColorSpace = availableFormats[0];
	std::cout << "selected swap chain format and color space: " << to_string(selectedFormatAndColorSpace.format) << " " << to_string(selectedFormatAndColorSpace.colorSpace) << std::endl;
	auto selectedPresentationMode = availablePresentationModes[0];
	std::cout << "selected presentation mode: " << to_string(selectedPresentationMode) << std::endl;
	// ------------------------------------------------------------------------------

	// ===> 7. Create a swapchain
	auto swapchainCreateInfo = vk::SwapchainCreateInfoKHR{}
		.setSurface(surface)
		.setImageExtent(vk::Extent2D{WIDTH, HEIGHT})
		.setMinImageCount(CONCURRENT_FRAMES)
		.setImageArrayLayers(1u)
		.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst)
		.setImageFormat(selectedFormatAndColorSpace.format)
		.setImageColorSpace(selectedFormatAndColorSpace.colorSpace)
		.setPresentMode(selectedPresentationMode);
	auto swapchain = device.createSwapchainKHR(swapchainCreateInfo);

	// ===> 8. Get all the swapchain's images
	auto swapchainImages = device.getSwapchainImagesKHR(swapchain);


	// ===> 10. Create a command pool so that we can create commands
	auto commandPoolCreateInfo = vk::CommandPoolCreateInfo{}
	.setQueueFamilyIndex(queueFamilyIndex);
	auto commandPool = device.createCommandPool(commandPoolCreateInfo);

	// Showing only a single color is -- for most of us probably -- a bit boring. Let's make something more fancy, shall we?
	// TODO Part 2: Load 100 images from file, which contain a sprite-animation of an explosion!
	//              The images can be loaded from: "images/explosion02HD-frame001.tga".."images/explosion02HD-frame100.tga"
	//              Feel free to use helpers::load_image_into_host_coherent_buffer
	// With these images loaded, we would like to copy them into the swapchain image.. just as we have done with the clear colors in Part 1.
	// HOWEVER, in order to achieve that, let's not create a new command buffer every frame, but instead, let's do something more Vulkan-esque:
	// TODO Part 2: Prepare a REUSABLE command buffer for every single one of the explosion-images to copy it into a given swap chain image!
	//              "Reusable" means: We create it once, let it live until the application's end, and use it whenever required.
	//              Attention: do not use vk::CommandBufferUsageFlagBits::eOneTimeSubmit for such command buffers, leave the flag away!
	std::array<std::tuple<vk::Buffer, vk::DeviceMemory, int, int>, 100> explosionImages;
	std::array<vk::CommandBuffer, 100> commandsBuffers;
	for (int i = 0; i < 100; i++) {
		std::string path = "images/explosion02HD-frame";
		if (i < 9)
			path += "00";
		else if (i < 99)
			path += "0";

		path += std::to_string(i+1) + ".tga";
		explosionImages[i] = helpers::load_image_into_host_coherent_buffer(physicalDevice, device, path);
		cleanupHandlers.emplace_back([device, buffer = std::get<vk::Buffer>(explosionImages[i])]() {device.destroyBuffer(buffer); });


		commandsBuffers[i] = helpers::allocate_command_buffer(device, commandPool);
		commandsBuffers[i].begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eRenderPassContinue));

		helpers::establish_pipeline_barrier_with_image_layout_transition(commandsBuffers[i],
			vk::PipelineStageFlagBits::eTopOfPipe, /* src -> dst */ vk::PipelineStageFlagBits::eTransfer,
			vk::AccessFlags{}, /* src -> dst */ vk::AccessFlagBits::eTransferWrite,
			swapchainImages[i % CONCURRENT_FRAMES], vk::ImageLayout::eUndefined, /* old -> new */ vk::ImageLayout::eTransferDstOptimal
		);
		// ------------------------------------------------------------------------------
		helpers::copy_buffer_to_image(commandsBuffers[i], std::get<vk::Buffer>(explosionImages[i]), swapchainImages[i % CONCURRENT_FRAMES], 800, 800);
		// ------------------------------------------------------------------------------
		helpers::establish_pipeline_barrier_with_image_layout_transition(commandsBuffers[i],
			vk::PipelineStageFlagBits::eTransfer, /* src -> dst */ vk::PipelineStageFlagBits::eBottomOfPipe,
			vk::AccessFlagBits::eTransferWrite, /* src -> dst */ vk::AccessFlags{},
			swapchainImages[i % CONCURRENT_FRAMES], vk::ImageLayout::eTransferDstOptimal, /* old -> new */ vk::ImageLayout::ePresentSrcKHR
		);
		// ------------------------------------------------------------------------------
		commandsBuffers[i].end();
	}



	// ===> 11. Start our render loop and clear those swap chain images!!
	const double startTime = glfwGetTime();
	int currentFrame = 0;
	while (!glfwWindowShouldClose(window)) {
		auto curTime = glfwGetTime();
		// Create a semaphore that will be signalled as soon as an image becomes available:
		auto imageAvailableSemaphore = device.createSemaphore(vk::SemaphoreCreateInfo{});
		// Request the next image (we'll get the index returned, we already have gotten the image handles in ===> 8.):
		auto swapChainImageIndex = device.acquireNextImageKHR(swapchain, std::numeric_limits<uint64_t>::max(), imageAvailableSemaphore, nullptr).value;
		auto& currentSwapchainImage = swapchainImages[swapChainImageIndex];


		// Create a semaphore that will be signalled when rendering has finished:
		auto renderFinishedSemaphore = device.createSemaphore(vk::SemaphoreCreateInfo{});
		// Submit the command buffer
		// ------------------------------------------------------------------------------
		// Task from Part 1: Can we wait in a specific/later stage?
		vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eTransfer;
		// ------------------------------------------------------------------------------
		// TODO Part 2: Submit the command buffer that you have selected!
		auto submitInfo = vk::SubmitInfo{}
			.setCommandBufferCount(1u)
			.setPCommandBuffers(&commandsBuffers[++currentFrame%100])
			.setWaitSemaphoreCount(1u)
			.setPWaitSemaphores(&imageAvailableSemaphore) // <--- (*1) Wait on the swap chain image to become available
			.setSignalSemaphoreCount(1u)
			.setPSignalSemaphores(&renderFinishedSemaphore) // Another semaphore: This will be signalled as soon as this batch of work has completed. 
			.setPWaitDstStageMask(&waitStage);
		queue.submit({ submitInfo }, nullptr);

		// Present the image to the screen:
		auto presentInfo = vk::PresentInfoKHR{}
			.setSwapchainCount(1u)
			.setPSwapchains(&swapchain)
			.setPImageIndices(&swapChainImageIndex)
			.setWaitSemaphoreCount(1u)
			.setPWaitSemaphores(&renderFinishedSemaphore); // Wait until rendering has finished (until vkQueueSubmit has signalled the renderFinishedSemaphore)
		queue.presentKHR(presentInfo);

		device.waitIdle();
		device.destroySemaphore(renderFinishedSemaphore);
		device.destroySemaphore(imageAvailableSemaphore);

		glfwPollEvents();
		if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
			glfwSetWindowShouldClose(window, GLFW_TRUE);
		}
	}

	// ===> 12. Perform cleanup
	for (auto it = cleanupHandlers.rbegin(); it != cleanupHandlers.rend(); ++it) {
		(*it)();
	}
	device.destroy(commandPool);
	device.destroy(swapchain);
	helpers::destroy_logical_device(device);
	helpers::destroy_surface(vkInst, surface);
	helpers::destroy_vulkan_instance(vkInst);
	helpers::destroy_window(window);
	glfwTerminate();
	return 0;
}
