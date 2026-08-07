// mem_view.h
// Maps the linear process memory onto a 2D image and samples it into an RGBA
// buffer for display. Works for both zoom-in (magnify, blocky) and zoom-out
// (overview, subsampled) with bounded per-frame work.
#pragma once

#include <cstdint>

#include "process_memory.h"

namespace mo {

enum class ColorMode {
    Grayscale = 0,  // 1 byte  -> 1 pixel
    RGB = 1,        // 3 bytes -> 1 pixel (byte order R,G,B)
    BGR = 2,        // 3 bytes -> 1 pixel (byte order B,G,R)
};

inline int BytesPerPixel(ColorMode m) { return m == ColorMode::Grayscale ? 1 : 3; }

// Camera over the memory image, expressed in image-pixel coordinates.
struct ViewState {
    int rowWidth = 1024;       // image pixels per row (i.e. bytes-per-row / bpp)
    ColorMode mode = ColorMode::Grayscale;
    double originX = 0.0;      // image-pixel coord at canvas top-left
    double originY = 0.0;
    double zoom = 1.0;         // screen pixels per image pixel

    uint64_t Stride() const {
        return static_cast<uint64_t>(rowWidth) * BytesPerPixel(mode);
    }
    uint64_t TotalRows(uint64_t totalBytes) const {
        uint64_t s = Stride();
        return s ? (totalBytes + s - 1) / s : 0;
    }
};

// Linear offset of an image pixel (ix,iy), or UINT64_MAX if out of range.
uint64_t PixelToOffset(const ViewState& vs, int64_t ix, int64_t iy,
                       uint64_t totalBytes);

// Fill an RGBA8 buffer (canvasW*canvasH*4) with the current view. Reads through
// `pm` one image-row at a time, caching the last row for the zoomed-in case.
void RenderView(ProcessMemory& pm, const ViewState& vs,
                int canvasW, int canvasH, uint8_t* outRGBA);

} // namespace mo
