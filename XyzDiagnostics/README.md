# XyzDiagnostics

XyzDiagnostics demonstrates how to inspect XYZ tile loading and rendering diagnostics in GeoKernel.

## Overview

It creates an OpenStreetMap XYZ layer with local caching enabled and displays runtime information about the layer, current view, requested tile range, cache directory, and renderer state. The toolbar provides standard map navigation and a refresh action so the diagnostic values can be inspected as the map is panned and zoomed.

## GIS Workflow

This example shows how to:

- Create an XYZ layer from a URL template
- Enable local caching for online tiles
- Inspect XYZ layer configuration
- Monitor the current map extent and scale
- Review tile request and cache information
- Refresh diagnostic values interactively
- Navigate an online Web Mercator map
- Investigate XYZ rendering behavior at runtime
