#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct ATUIFontFace {
	std::string family;
	std::string style;
	int index = 0;
	bool monospaced = false;
	bool italic = false;
	bool requiresNativeRenderer = false;
};

// Read bounded SFNT tables, including every collection face. Named variable
// instances use FreeType's packed face/instance index when requested.
std::vector<ATUIFontFace> ATUIReadFontMetadata(const void *data, size_t size,
	bool includeNamedInstances);
