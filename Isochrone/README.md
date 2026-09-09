# Isochrone

Downloads the Stockholm road dataset and calculates travel-time accessibility
from a user-selected origin.

The origin is snapped to the largest connected road network. A full-network
`RoutingDijkstra::run()` call uses `RoutingCostMetric::TravelTime`, edge speeds,
and one-way restrictions to classify reachable roads into 0–5, 5–10, and
10–15 minute bands.

Green, orange, and red bands are drawn as overlays without changing map layers
or the visible extent. Select a row in the right panel to emphasize a band and
inspect cumulative reachable-node and band-edge counts.

The source data remains in WGS84 (`EPSG:4326`), while the viewer uses Web
Mercator (`EPSG:3857`) for display.
