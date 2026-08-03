# OnTheFlyReproject

OnTheFlyReproject demonstrates how to reproject a vector layer dynamically into different viewer coordinate systems with GeoKernel.

## Overview

It prepares the world boundary sample data in EPSG:4326, loads the layer into a map viewer, and lets the user switch the viewer between geographic, Mercator, Miller, Mollweide, Sinusoidal, and Eckert coordinate systems. The source layer keeps its original CRS while GeoKernel reprojects it during rendering, and the status bar reports mouse coordinates in the active viewer CRS.

## GIS Workflow

This example shows how to:

- Download and load vector sample data
- Assign the source coordinate system to a layer
- Create target coordinate systems from EPSG and ESRI authority codes
- Change the viewer coordinate system at runtime
- Reproject a vector layer during rendering
- Compare multiple world projection types
- Display mouse coordinates in the active CRS
- Reset the map to the projection-specific full extent
