#include "core/libcamera_app.hpp"

#define LOG(level, text)                                                                                               \
	do                                                                                                                 \
	{                                                                                                                  \
		if (RPiCamApp::GetVerbosity() >= level)                                                                     \
			std::cerr << text << std::endl;                                                                            \
	} while (0)

// LOG_ERROR define conflicts with the boost definition
// If boost is in use, let the pre-existing #def take over.
#ifndef LOG_ERROR
	#define LOG_ERROR(text) std::cerr << text << std::endl
#endif
