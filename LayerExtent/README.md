# LayerExtent

LayerExtent demonstrates how to read and visualize a layer's geographic extent in a GeoKernel viewer.

## Overview

It prepares the required California sample data, loads the California boundary layer, calculates the layer extent, and draws a polygon rectangle around that extent. The result shows how layer bounds can be used to create helper geometry or visual diagnostics on the map.

## GIS Workflow

This example shows the basic workflow for:

- Initialize a map viewer
- Download and load vector sample data
- Read a layer extent
- Create geometry from extent coordinates
- Add an extent rectangle overlay to the map
