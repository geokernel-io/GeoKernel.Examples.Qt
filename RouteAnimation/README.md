# RouteAnimation

Downloads the Stockholm road dataset, builds the routing graph, and animates a
vehicle along a user-selected shortest route.

The first click selects the green start marker and the second selects the red
finish marker. Snapping is restricted to the main connected network and to
nodes reachable from the selected start. Route geometry and animation are
drawn as overlays, so playback never changes map layers or the visible extent.

Use **Play**, **Pause**, and **Reset** in the right panel. During playback the
panel reports progress, remaining distance, and remaining travel time. The
animation duration adapts to route length while the reported values retain the
SDK's real distance and travel-time units. Ordered road names are read from the
routing edge `name` attribute.

The source data remains in WGS84 (`EPSG:4326`), while the viewer uses Web
Mercator (`EPSG:3857`) for display.
