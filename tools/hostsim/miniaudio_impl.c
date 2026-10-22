/* The miniaudio implementation TU for armlive (vendored single header,
 * third_party/miniaudio.h, public domain / MIT-0 - see third_party/LICENSE.miniaudio).
 * Only the low-level device API is used. */
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include "third_party/miniaudio.h"
