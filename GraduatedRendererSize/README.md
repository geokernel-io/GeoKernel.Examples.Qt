# GraduatedRendererSize

GraduatedRendererSize demonstrates attribute-driven point symbol scaling with GeoKernel.

## Overview

It downloads the USA cities sample data, loads the city points, derives numeric size classes from the POP_CLASS attribute, and applies a graduated size renderer so larger population classes are drawn with larger point symbols. The example also builds a readable legend and opens the map zoomed to the loaded city data extent.

## GIS Workflow

This example shows how to:

- Load remote vector sample data
- Create a graduated size renderer
- Scale point symbols by attribute values
- Display size classes in a legend
- Zoom the viewer to the active data extent
