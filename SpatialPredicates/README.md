# SpatialPredicates

SpatialPredicates demonstrates how to evaluate common spatial relationships directly with GeoKernel topology predicates.

## Overview

It creates multiple polygon and polyline pairs entirely in memory, arranging each pair to demonstrate a specific relationship. The viewer displays labeled examples for contains, within, touches, overlaps, crosses, and disjoint, while a details panel evaluates every predicate and reports the result together with the source geometry extents.

## GIS Workflow

This example shows how to:

- Create polygon and polyline geometries in memory
- Arrange geometry pairs for spatial relationship tests
- Display and label multiple predicate examples
- Initialize the GeoKernel topology engine
- Evaluate contains and within relationships
- Test touches, overlaps, crosses, and disjoint states
- Inspect the extent of each input geometry
- Present multiple predicate results in one viewer
