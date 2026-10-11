#pragma once

#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE

#if __has_include( <tiny_gltf.h>)
#define PIKO_HAS_TINYGLTF 1
#include <tiny_gltf.h>
#else
#define PIKO_HAS_TINYGLTF 0
#endif
