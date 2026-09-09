# XyzTileSize

XyzTileSize demonstrates how the configured tile pixel size affects XYZ layer behavior in GeoKernel.

## Overview

It opens two side-by-side viewers using the same OpenStreetMap URL template while configuring one layer for 256-pixel tiles and the other for 512-pixel tiles. Shared navigation actions make the two configurations easy to compare, and separate cache directories keep each tile-size variant isolated. A details panel explains when standard and high-DPI tile sizes should be used.

## GIS Workflow

This example shows how to:

- Create multiple XYZ tile layers from one URL template
- Configure layer tile size with `setTileSize`
- Compare 256-pixel and 512-pixel tile settings
- Use separate cache directories per tile size
- Display synchronized map configurations side by side
- Apply navigation tools to multiple viewers
- Work with online Web Mercator tile layers
- Understand standard and high-DPI tile services
