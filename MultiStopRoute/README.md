# MultiStopRoute

Downloads the Stockholm road dataset and calculates one continuous route
through user-selected stops in selection order.

Click the map to add the green start, one or more numbered intermediate stops,
and the red current finish. Press **Calculate route** after selecting at least
two stops. **New multi-stop route** clears the route and all stops.

Every stop is snapped within 2 km. The first stop is restricted to the largest
connected road network; every following stop is restricted to nodes reachable
from the preceding stop. This prevents disconnected or one-way-incompatible
legs.

The right panel reports each leg, total distance and time, and ordered road
names from the routing edge `name` attribute. Click a leg to highlight the
complete stop-to-stop geometry, or click a road-direction row to highlight
only that named road segment. The source data remains in WGS84
(`EPSG:4326`), while the viewer uses Web Mercator (`EPSG:3857`) for display.
