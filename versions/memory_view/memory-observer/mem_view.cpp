// mem_view.cpp
#include "mem_view.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace mo {

uint64_t PixelToOffset(const ViewState& vs, int64_t ix, int64_t iy,
                       uint64_t totalBytes) {
    if (ix < 0 || iy < 0 || ix >= vs.rowWidth)
        return UINT64_MAX;
    int bpp = BytesPerPixel(vs.mode);
    uint64_t off = static_cast<uint64_t>(iy) * vs.Stride() +
                   static_cast<uint64_t>(ix) * bpp;
    if (off + static_cast<uint64_t>(bpp) > totalBytes)
        return UINT64_MAX;
    return off;
}

static inline void WritePixel(uint8_t* px, const uint8_t* src, ColorMode mode,
                              int bpp) {
    if (bpp == 1) {
        px[0] = px[1] = px[2] = src[0];
    } else if (mode == ColorMode::RGB) {
        px[0] = src[0];
        px[1] = src[1];
        px[2] = src[2];
    } else {  // BGR
        px[0] = src[2];
        px[1] = src[1];
        px[2] = src[0];
    }
    px[3] = 255;
}

void RenderView(ProcessMemory& pm, const ViewState& vs,
                int canvasW, int canvasH, uint8_t* outRGBA) {
    if (canvasW <= 0 || canvasH <= 0)
        return;

    const int bpp = BytesPerPixel(vs.mode);
    const uint64_t stride = vs.Stride();
    const uint64_t total = pm.TotalSize();
    const uint64_t totalRows = vs.TotalRows(total);
    const double zoom = vs.zoom > 1e-9 ? vs.zoom : 1e-9;

    // Horizontal image-pixel range visible this frame (constant per frame).
    int64_t ix0 = static_cast<int64_t>(std::floor(vs.originX));
    int64_t ix1 = static_cast<int64_t>(std::ceil(vs.originX + canvasW / zoom));
    ix0 = std::max<int64_t>(ix0, 0);
    ix1 = std::min<int64_t>(ix1, vs.rowWidth);

    std::vector<uint8_t> rowBuf;
    int64_t cachedRow = -1;

    for (int sy = 0; sy < canvasH; ++sy) {
        uint8_t* dst = outRGBA + static_cast<size_t>(sy) * canvasW * 4;
        int64_t iy = static_cast<int64_t>(std::floor(vs.originY + sy / zoom));

        if (iy < 0 || static_cast<uint64_t>(iy) >= totalRows || ix1 <= ix0) {
            std::memset(dst, 0, static_cast<size_t>(canvasW) * 4);
            continue;
        }

        // Read this image row once (reused across screen rows when zoomed in).
        if (iy != cachedRow) {
            uint64_t off = static_cast<uint64_t>(iy) * stride +
                           static_cast<uint64_t>(ix0) * bpp;
            size_t len = static_cast<size_t>(ix1 - ix0) * bpp;
            rowBuf.assign(len, 0);
            if (off < total) {
                size_t want = static_cast<size_t>(
                    std::min<uint64_t>(len, total - off));
                pm.ReadLinear(off, rowBuf.data(), want);
            }
            cachedRow = iy;
        }

        const uint64_t rowBase = static_cast<uint64_t>(iy) * stride;
        for (int sx = 0; sx < canvasW; ++sx) {
            uint8_t* px = dst + static_cast<size_t>(sx) * 4;
            int64_t ix = static_cast<int64_t>(std::floor(vs.originX + sx / zoom));
            if (ix < ix0 || ix >= ix1) {
                px[0] = px[1] = px[2] = 0;
                px[3] = 255;
                continue;
            }
            // Bytes past the end of the linear space render as black.
            uint64_t off = rowBase + static_cast<uint64_t>(ix) * bpp;
            if (off + static_cast<uint64_t>(bpp) > total) {
                px[0] = px[1] = px[2] = 0;
                px[3] = 255;
                continue;
            }
            size_t bo = static_cast<size_t>(ix - ix0) * bpp;
            WritePixel(px, &rowBuf[bo], vs.mode, bpp);
        }
    }
}

} // namespace mo
