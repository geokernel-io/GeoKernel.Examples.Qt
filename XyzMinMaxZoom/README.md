# XyzMinMaxZoom

XyzMinMaxZoom demonstrates how to restrict the available zoom levels of an XYZ tile layer in GeoKernel.

## Overview

It creates an OpenStreetMap XYZ layer and lets the user configure its minimum and maximum tile zoom levels at runtime. Preset buttons make low, medium, and high zoom ranges easy to compare, while the details panel explains the active limits, cache location, and relevant `GisLayerXYZ` API calls. Standard map navigation tools show how the configured range affects tile selection as the viewer zooms in and out.

## GIS Workflow

This example shows how to:

- Create an XYZ layer from a URL template
- Configure minimum and maximum tile zoom levels
- Normalize an invalid reversed zoom range
- Apply predefined zoom ranges interactively
- Use a separate cache directory for each range
- Inspect active XYZ layer settings
- Navigate an online Web Mercator map
- Understand how zoom limits affect tile requests
