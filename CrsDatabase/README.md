# CrsDatabase

CrsDatabase demonstrates how to look up coordinate reference systems from the EPSG database with GeoKernel.

## Overview

It accepts an EPSG code, resolves the matching coordinate system through `CoordinateSystemFactory`, and displays its name, geographic or projected type, unit conversion, and complete GDAL/PROJ definition. Invalid or unsupported codes are reported directly in the interface.

## GIS Workflow

This example shows how to:

- Look up coordinate systems by EPSG code
- Use `CoordinateSystemFactory::fromEpsg`
- Inspect geographic and projected coordinate systems
- Read coordinate system names and EPSG identifiers
- Inspect meters-per-unit information
- Display the complete coordinate system definition
- Handle invalid or unsupported EPSG codes
