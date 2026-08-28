/* Single translation unit that provides stb_image's implementation --
 * see stb_image.h. Only JPEG decoding (Reolink snapshots) is actually
 * used by this project; the other format decoders it also builds in are
 * left as-is rather than trimmed out, matching upstream's single-header
 * "just vendor the whole thing" design. */
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
