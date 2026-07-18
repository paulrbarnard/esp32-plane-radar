#pragma once

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();
/** Select an aircraft near a tap; any tap dismisses an open detail card. */
void radarDisplayHandleTap(int x, int y);

}  // namespace ui
