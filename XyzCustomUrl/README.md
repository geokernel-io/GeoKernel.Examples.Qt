# XyzCustomUrl

XyzCustomUrl demonstrates how to create an XYZ tile layer from a user-defined URL template with GeoKernel.

## Overview

It provides toolbar controls for entering an XYZ or Bing-style tile URL, configuring minimum and maximum zoom levels, and enabling local caching. Applying the settings creates and opens a `GisLayerXYZ`, displays its configuration in a details panel, and opens the online map at a Web Mercator extent. The example validates required URL placeholders before loading the layer.

## GIS Workflow

This example shows how to:

- Create an XYZ tile layer manually
- Configure a custom tile URL template
- Validate XYZ and QuadKey URL placeholders
- Set minimum and maximum tile zoom levels
- Enable or disable local tile caching
- Open and add an online tile layer to the viewer
- Inspect the active layer configuration
- Handle invalid templates and loading errors
