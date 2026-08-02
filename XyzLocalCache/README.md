# XyzLocalCache

XyzLocalCache demonstrates how to store and reuse online XYZ map tiles with GeoKernel's local disk cache.

## Overview

It loads an OpenStreetMap XYZ layer, allows local caching to be enabled or disabled, and lets the user choose the cache directory. As the map is panned and zoomed, downloaded tiles are stored for reuse. The details panel reports the active URL, cache location, tile-file count, and disk usage, while toolbar actions can refresh statistics or clear cached tiles.

## GIS Workflow

This example shows how to:

- Create and open an online XYZ tile layer
- Enable or disable local tile caching
- Configure a custom cache directory
- Browse for a cache location at runtime
- Count cached tile files and disk usage
- Reuse downloaded tiles between application runs
- Refresh cache statistics interactively
- Clear cached tile data safely from the selected directory
