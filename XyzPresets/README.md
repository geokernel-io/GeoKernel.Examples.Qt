# XyzPresets

XyzPresets demonstrates how to browse and load predefined XYZ tile services with GeoKernel.

## Overview

It reads the built-in XYZ layer presets, lists them in a toolbar selector, and creates a `GisLayerXYZ` from the selected preset's URL template, zoom limits, tile size, and attribution. The user can switch providers at runtime, enable or disable local tile caching, inspect the active preset configuration, and navigate the Web Mercator map with standard viewer tools.

## GIS Workflow

This example shows how to:

- Read the predefined XYZ layer preset collection
- Create an XYZ tile layer from preset metadata
- Switch online tile providers at runtime
- Configure local tile caching
- Inspect URL templates, zoom limits, and attribution
- Work with a Web Mercator map extent
- Navigate with pan and zoom tools
- Handle online tile layer loading errors
