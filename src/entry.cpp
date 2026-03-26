#include "plugin.h"
#include <clap/clap.h>
#include <cstring>

// ============================================================
// CLAP Plugin Descriptor
// ============================================================

static const char* features[] = {
    CLAP_PLUGIN_FEATURE_UTILITY,
    CLAP_PLUGIN_FEATURE_NOTE_EFFECT,
    nullptr
};

static const clap_plugin_descriptor_t sDescriptor = {
    CLAP_VERSION,
    "com.xyttra.midi-capture",
    "MIDI Capture",
    "Xyttra",
    "https://github.com/xyttra",
    "",
    "",
    "1.0.0",
    "Continuously records MIDI into a 10-minute ring buffer. Press COPY to export.",
    features
};

// ============================================================
// CLAP Plugin Callbacks (forwarding to MidiCapture class)
// ============================================================

static bool plugin_init(const clap_plugin_t* plugin) {
    auto* p = static_cast<MidiCapture*>(plugin->plugin_data);
    return p->init();
}

static void plugin_destroy(const clap_plugin_t* plugin) {
    auto* p = static_cast<MidiCapture*>(plugin->plugin_data);
    p->destroy();
    // Don't delete plugin struct — it's part of MidiCapture
}

static bool plugin_activate(const clap_plugin_t* plugin, double sr, uint32_t minf, uint32_t maxf) {
    return static_cast<MidiCapture*>(plugin->plugin_data)->activate(sr, minf, maxf);
}

static void plugin_deactivate(const clap_plugin_t* plugin) {
    static_cast<MidiCapture*>(plugin->plugin_data)->deactivate();
}

static bool plugin_start_processing(const clap_plugin_t* plugin) {
    return static_cast<MidiCapture*>(plugin->plugin_data)->startProcessing();
}

static void plugin_stop_processing(const clap_plugin_t* plugin) {
    static_cast<MidiCapture*>(plugin->plugin_data)->stopProcessing();
}

static void plugin_reset(const clap_plugin_t* plugin) {
    static_cast<MidiCapture*>(plugin->plugin_data)->reset();
}

static clap_process_status plugin_process(const clap_plugin_t* plugin, const clap_process_t* process) {
    return static_cast<MidiCapture*>(plugin->plugin_data)->process(process);
}

static const void* plugin_get_extension(const clap_plugin_t* plugin, const char* id) {
    return static_cast<MidiCapture*>(plugin->plugin_data)->getExtension(id);
}

static void plugin_on_main_thread(const clap_plugin_t* plugin) {
    static_cast<MidiCapture*>(plugin->plugin_data)->onMainThread();
}

// ============================================================
// Plugin Factory
// ============================================================

static uint32_t factory_get_plugin_count(const clap_plugin_factory_t* factory) {
    return 1;
}

static const clap_plugin_descriptor_t* factory_get_plugin_descriptor(
    const clap_plugin_factory_t* factory, uint32_t index) {
    return index == 0 ? &sDescriptor : nullptr;
}

static const clap_plugin_t* factory_create_plugin(
    const clap_plugin_factory_t* factory,
    const clap_host_t* host,
    const char* plugin_id)
{
    if (strcmp(plugin_id, sDescriptor.id) != 0)
        return nullptr;

    auto* capture = new MidiCapture(host);

    // Allocate the clap_plugin_t struct
    auto* plugin = new clap_plugin_t();
    plugin->desc = &sDescriptor;
    plugin->plugin_data = capture;
    plugin->init = plugin_init;
    plugin->destroy = [](const clap_plugin_t* p) {
        auto* cap = static_cast<MidiCapture*>(p->plugin_data);
        delete cap;
        delete p;
    };
    plugin->activate = plugin_activate;
    plugin->deactivate = plugin_deactivate;
    plugin->start_processing = plugin_start_processing;
    plugin->stop_processing = plugin_stop_processing;
    plugin->reset = plugin_reset;
    plugin->process = plugin_process;
    plugin->get_extension = plugin_get_extension;
    plugin->on_main_thread = plugin_on_main_thread;

    capture->_plugin = plugin;

    return plugin;
}

static const clap_plugin_factory_t sFactory = {
    factory_get_plugin_count,
    factory_get_plugin_descriptor,
    factory_create_plugin,
};

// ============================================================
// CLAP Entry Point
// ============================================================

static bool entry_init(const char* path) {
    return true;
}

static void entry_deinit() {}

static const void* entry_get_factory(const char* factory_id) {
    if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0)
        return &sFactory;
    return nullptr;
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION,
    entry_init,
    entry_deinit,
    entry_get_factory,
};
