# AlternativeRoutes

Downloads the small Stockholm road dataset, builds a directed routing graph,
and calculates up to three alternative routes between two user-selected
points. The first map click selects the green start marker; the second selects
the red finish marker and calculates the alternatives. Another click starts a
new selection. Each click snaps to the nearest routing node within 2 km.

The first result is the shortest route. Further candidates are calculated by
penalizing already-used graph edges, encouraging useful alternatives without
disconnecting the road network. Select an alternative in the right panel to
highlight it and view its ordered road names and segment distances.

The example demonstrates `buildRoutingGraphForLayer()`, `routingGraph()`,
`RoutingGraph::outEdges()`, routing edge attributes, and custom graph search.
The source data remains in WGS84 (`EPSG:4326`), while the viewer uses Web
Mercator (`EPSG:3857`) for display.
