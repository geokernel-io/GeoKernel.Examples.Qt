# SnappingEnabled

SnappingEnabled demonstrates how to enable snapping and control its tolerance while digitizing in GeoKernel.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates an editable in-memory polyline layer with a guide feature. The user can draw new lines near the guide, toggle snapping on or off, and adjust the snapping tolerance in pixels to compare how digitized vertices align with existing geometry.

## GIS Workflow

This example shows how to:

- Create an editable in-memory polyline layer
- Add a guide feature as a snapping target
- Activate the interactive polyline digitizing tool
- Enable or disable edit snapping
- Configure snapping tolerance in screen pixels
- Compare snapped and unsnapped geometry
- Reset the guide layer and refresh the map
