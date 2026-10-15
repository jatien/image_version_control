// stb_image and stb_image_write are single-header libraries: the
// implementation is compiled exactly once, in this file. Every other file
// just includes the headers.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
