# LayerLoadOptions

LayerLoadOptions demonstrates how to control vector layer loading behavior with GeoKernel layer load options.

## Overview

It prepares the required USA states sample data, then lets the user load the same layer either without a spatial index or with an RTree spatial index. The example shows load progress, reports index preparation status, and provides a query benchmark to compare feature hit-test performance between the two loading modes.

## GIS Workflow

This example shows the basic workflow for:

- Initialize a map viewer
- Download and load vector sample data
- Configure layer loading options
- Enable or disable spatial indexing
- Benchmark spatial query performance
