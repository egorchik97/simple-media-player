#pragma once

#include "Appearance.h"

namespace media::appearance_detail {

// Paint only foreground controls; the caller owns the background and snapshots.
void PaintControls(HDC dc, const RenderState& state);

} // namespace media::appearance_detail