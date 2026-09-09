# RouteOptimization

Downloads the Stockholm road dataset and optimizes closed visit tours for a
user-selected number of service vehicles.

The first selected point is the green depot (`D`); subsequent orange markers
are numbered visits. Select at least two visits, enter the service vehicle
count, and press **Optimize route**. Every vehicle starts and ends at the depot,
and every visit is assigned to exactly one vehicle.

The example builds a directed road-distance matrix with
`RoutingDijkstra::allPairsDistances()`. A nearest-neighbor seed is improved by
repeated pair swaps. The optimized visit sequence is partitioned across the
requested fleet with balanced visit counts. Each vehicle tour is then expanded
back to its real road geometry, distance, travel time, and routing edge `name`
attributes.

The right panel displays fleet totals and one row per vehicle, including its
visit order, distance, and time. Each vehicle uses a separate map color. Click
a vehicle row or road direction to highlight its geometry. Snapping is
restricted to the connected road network.

Selecting a vehicle also filters **Road directions** to that vehicle's own
ordered edge sequence; selecting a road row highlights only that segment of
the selected vehicle tour.

The source data remains in WGS84 (`EPSG:4326`), while the viewer uses Web
Mercator (`EPSG:3857`) for display.
