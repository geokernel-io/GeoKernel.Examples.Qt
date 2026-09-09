# CrsByAuthority

CrsByAuthority demonstrates how to create coordinate systems from authority names and numeric codes with GeoKernel.

## Overview

It provides editable authority and code controls, resolves entries such as EPSG, ESRI, and IGNF definitions through `CoordinateSystemFactory`, and displays the resulting coordinate system name, type, unit conversion, EPSG code, and full definition.

## GIS Workflow

This example shows how to:

- Create coordinate systems from authority codes
- Resolve EPSG, ESRI, and IGNF definitions
- Inspect geographic and projected coordinate systems
- Read coordinate system names and EPSG codes
- Inspect meters-per-unit information
- Display the complete coordinate system definition
- Handle invalid or unsupported authority codes
