#include "farm_live_stream.h"
#include "worm_audio.h"
#include "worm_farm_sim.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

static void initialize_wormfarm(ModuleInitializationLevel level)
{
	if (level != MODULE_INITIALIZATION_LEVEL_SCENE)
		return;
	GDREGISTER_CLASS(WormFarmSim);
	GDREGISTER_CLASS(WormAudioSynth);
	GDREGISTER_CLASS(FarmLiveStream);
}

static void uninitialize_wormfarm(ModuleInitializationLevel level)
{
	(void)level;
}

extern "C" {
GDExtensionBool GDE_EXPORT wormfarm_library_init(GDExtensionInterfaceGetProcAddress get_proc_address, GDExtensionClassLibraryPtr library,
                                                 GDExtensionInitialization *initialization)
{
	GDExtensionBinding::InitObject init(get_proc_address, library, initialization);
	init.register_initializer(initialize_wormfarm);
	init.register_terminator(uninitialize_wormfarm);
	init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init.init();
}
}
