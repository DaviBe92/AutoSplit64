/**
 * OBS Plugin for AutoSplit64+
 * This plugin captures video frames from OBS and shares them through shared memory
 * for Autosplit64+ to consume.
 */

#include <obs-module.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "constants.h"

OBS_DECLARE_MODULE()

/**
 * Filter data structure that holds the state of our video capture filter
 */
struct filter_data {
	obs_source_t *context;   // OBS source context
	gs_texrender_t *render;  // Graphics render target
	gs_stagesurf_t *staging; // Staging surface for CPU access
	uint32_t width;          // Current frame width
	uint32_t height;         // Current frame height
	uint8_t *image_data;     // Buffer for BGR pixel data
	HANDLE shmem;            // Windows shared memory handle
	LPCTSTR pBuf;            // Pointer to the mapped view of the shared memory
	uint32_t frame_counter;  // Frame counter for synchronization
	bool shmem_valid;        // Track if shared memory is valid
};

/**
 * Returns the display name of the filter
 *
 * @param unused Unused parameter
 * @return The display name of the filter
 */
static const char *filter_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "AS64+ Frame grabber";
}

/**
 * Creates a new instance of the filter
 * Initializes rendering resources and shared memory
 *
 * @param settings OBS data settings
 * @param source OBS source
 * @return Pointer to the created filter data
 */
static void *filter_create(obs_data_t *settings, obs_source_t *source)
{
	// Create the filter data
	struct filter_data *filter = bzalloc(sizeof(struct filter_data));
	if (!filter) {
		blog(LOG_ERROR, "Failed to allocate filter data");
		return NULL;
	}

	filter->context = source;
	filter->render = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	if (!filter->render) {
		blog(LOG_ERROR, "Failed to create texture renderer");
		bfree(filter);
		return NULL;
	}

	filter->frame_counter = 0;
	filter->shmem_valid = false;

	return filter;
}

/**
 * Destroys the filter data and releases all associated resources.
 * This function is called when the filter is destroyed.
 *
 * @param data Pointer to the filter data to be destroyed.
 */
static void filter_destroy(void *data)
{
	struct filter_data *filter = data;
	if (filter) {
		if (filter->render)
			gs_texrender_destroy(filter->render);
		if (filter->staging)
			gs_stagesurface_destroy(filter->staging);
		if (filter->image_data)
			bfree(filter->image_data);
		// Unmap and close the shared memory
		if (filter->pBuf)
			UnmapViewOfFile(filter->pBuf);
		if (CloseHandle(filter->shmem)) {
			blog(LOG_INFO, "Closed the shared memory");
		} else {
			blog(LOG_ERROR, "Failed to close the shared memory");
		}
		bfree(filter);
	}
}

/**
 * Creates the properties for the filter
 *
 * @param data Pointer to the filter data
 * @return Pointer to the created properties
 */
static bool github_button_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	system("start " GITHUB_URL);
	return false;
}

static bool discord_button_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	system("start " DISCORD_URL);
	return false;
}

static obs_properties_t *filter_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();

	// Info group
	obs_properties_add_text(props, "plugin_description", "AutoSplit64+ Frame Grabber", OBS_TEXT_INFO);
	obs_properties_add_text(props, "description",
				"This plugin captures frames from the video source and shares them with "
				"AutoSplit64+.\nAdd this filter to any video source "
				"you want AutoSplit64+ to analyze.",
				OBS_TEXT_INFO);

	// Social links - directly in props instead of a group for horizontal layout
	obs_properties_add_button(props, "github_link", "📂 GitHub Repository", github_button_clicked);
	obs_properties_add_button(props, "discord_link", "💬 Join Discord", discord_button_clicked);

	// Version and author info - directly use the constants
	obs_properties_add_text(props, "version", PLUGIN_VERSION, OBS_TEXT_INFO);
	obs_properties_add_text(props, "author", PLUGIN_AUTHOR, OBS_TEXT_INFO);

	return props;
}

/**
 * Attempts to recover the shared memory connection
 *
 * @param filter Pointer to the filter data
 */
static void try_recover_shmem(struct filter_data *filter)
{
	if (filter->pBuf) {
		UnmapViewOfFile(filter->pBuf);
		filter->pBuf = NULL;
	}
	if (filter->shmem) {
		CloseHandle(filter->shmem);
		filter->shmem = NULL;
	}
	filter->shmem_valid = false;
}

/**
 * Opens shared memory for the given dimensions.
 *
 * @param filter Pointer to the filter data
 * @param width Frame width
 * @param height Frame height
 * @return True if shared memory was successfully opened, false otherwise
 */
static bool open_shmem(struct filter_data *filter, uint32_t width, uint32_t height)
{
	if (filter->shmem && filter->shmem_valid) {
		// Already open, no need to recreate
		return true;
	}
	try_recover_shmem(filter);
	filter->shmem = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 16 + width * height * 4,
					  TEXT("as64_grabber"));
	if (filter->shmem) {
		filter->pBuf = MapViewOfFile(filter->shmem, FILE_MAP_ALL_ACCESS, 0, 0, 16 + width * height * 4);
		if (filter->pBuf) {
			filter->shmem_valid = true;
			blog(LOG_INFO, "Opened shared memory connection");
			return true;
		} else {
			blog(LOG_ERROR, "Failed to map shared memory: %lu", GetLastError());
			CloseHandle(filter->shmem);
			filter->shmem = NULL;
		}
	} else {
		blog(LOG_ERROR, "Failed to create shared memory");
	}
	return false;
}

/**
 * Handles shared memory mapping and data transfer.
 *
 * @param filter Pointer to the filter data
 * @param width Frame width
 * @param height Frame height
 * @return True if data was successfully transferred, false otherwise
 */
static bool handle_shared_memory(struct filter_data *filter, uint32_t width, uint32_t height)
{
	if (!filter->shmem_valid) {
		if (!open_shmem(filter, width, height)) {
			return false;
		}
	}

	if (filter->pBuf) {
		uint32_t *header = (uint32_t *)filter->pBuf;
		header[0] = width;
		header[1] = height;
		header[2] = width * 4;
		header[3] = filter->frame_counter++;
		memcpy((uint8_t *)filter->pBuf + 16, filter->image_data, width * height * 4);
		return true;
	} else {
		blog(LOG_ERROR, "Shared memory view is not mapped");
		filter->shmem_valid = false;
	}

	return false;
}

/**
 * Main rendering function that:
 * 1. Captures the video frame
 * 2. Shares it through shared memory
 * 3. Renders the frame to output
 *
 * @param data Pointer to the filter data
 * @param effect OBS effect
 */
static void filter_render(void *data, gs_effect_t *effect)
{
	struct filter_data *filter = data;
	if (!filter)
		return;

	obs_source_t *target = obs_filter_get_target(filter->context);
	if (!target) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	uint32_t width = obs_source_get_width(target);
	uint32_t height = obs_source_get_height(target);

	if (width == 0 || height == 0) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	if (width != filter->width || height != filter->height) {
		filter->width = width;
		filter->height = height;
		if (filter->staging)
			gs_stagesurface_destroy(filter->staging);
		filter->staging = gs_stagesurface_create(width, height, GS_BGRA);
		if (!filter->staging) {
			blog(LOG_ERROR, "Failed to allocate staging surface for %dx%d", width, height);
			obs_source_skip_video_filter(filter->context);
			return;
		}
		if (!filter->image_data) {
			filter->image_data = bzalloc(width * height * 4);
		} else {
			filter->image_data = brealloc(filter->image_data, width * height * 4);
		}
		if (!filter->image_data) {
			blog(LOG_ERROR, "Failed to allocate image data for %dx%d", width, height);
			obs_source_skip_video_filter(filter->context);
			return;
		}
		if (!open_shmem(filter, width, height)) {
			obs_source_skip_video_filter(filter->context);
			return;
		}
	}

	gs_texrender_reset(filter->render);

	if (gs_texrender_begin(filter->render, width, height)) {
		if (!effect)
			effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);

		gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		obs_source_video_render(target);
		gs_blend_state_pop();
		gs_texrender_end(filter->render);

		gs_stage_texture(filter->staging, gs_texrender_get_texture(filter->render));
		uint8_t *data;
		uint32_t linesize;
		if (gs_stagesurface_map(filter->staging, &data, &linesize)) {
			for (uint32_t row = 0; row < height; row++) {
				memcpy(filter->image_data + (row * width * 4), data + (row * linesize), width * 4);
			}
			gs_stagesurface_unmap(filter->staging);
		}

		handle_shared_memory(filter, width, height);
	}

	if (!effect)
		effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);

	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	if (image) {
		gs_effect_set_texture(image, gs_texrender_get_texture(filter->render));
		while (gs_effect_loop(effect, "Draw"))
			gs_draw_sprite(gs_texrender_get_texture(filter->render), 0, width, height);
	}
}

/**
 * Source info structure that registers the filter with OBS
 */
struct obs_source_info image_capture_filter = {
	.id = "image_capture_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = filter_name,
	.create = filter_create,
	.destroy = filter_destroy,
	.get_properties = filter_properties,
	.video_render = filter_render,
};

/**
 * Module entry point
 * Registers the filter with OBS when the module is loaded
 *
 * @return True if the module was successfully loaded, false otherwise
 */
bool obs_module_load(void)
{
	obs_register_source(&image_capture_filter);
	return true;
}
