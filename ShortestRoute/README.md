# ShortestRoute

Downloads the small Stockholm road dataset, builds a directed routing graph,
and calculates the shortest route between two user-selected points. The first
map click selects the green start marker; the second selects the red finish
marker and calculates the route. Another click starts a new selection.
Each click snaps to the nearest routing node within 2 km.
Use **Select route points** to return to route selection after navigating with
the pan or zoom tools.

The example demonstrates `buildRoutingGraphForLayer()` and
`addShortestRouteLayerBetweenPoints()`. Distances are reported in meters and
travel times in seconds by the SDK.

The source road data remains in WGS84 (`EPSG:4326`), while the viewer uses
Web Mercator (`EPSG:3857`) so Stockholm is displayed with the correct visual
aspect ratio.
The Full Extent action uses the projected Stockholm extent as well.
