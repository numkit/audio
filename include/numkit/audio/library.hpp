/// @file library.hpp
/// @ingroup group_audio
#pragma once

namespace numkit {

/// @addtogroup group_audio
class PluginContext;

class AudioLibrary
{
public:
    static void install(PluginContext &engine);
};

/// @}
} // namespace numkit
