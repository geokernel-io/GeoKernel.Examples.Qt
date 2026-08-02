# BusyCallback

BusyCallback demonstrates how to observe viewer busy state and layer loading progress while processing a large dataset with GeoKernel.

## Overview

It prepares a one-million-point sample dataset, loads it with RTree spatial index creation enabled, and connects to viewer and loading callbacks. The interface displays busy state, progress, status messages, spatial index preparation stages, layer events, and elapsed load time while keeping a timestamped event log for the full operation.

## GIS Workflow

This example shows how to:

- Download and prepare a large vector sample dataset
- Configure layer loading with an RTree spatial index
- Observe the viewer's `busyChanged` signal
- Report layer load progress and status callbacks
- Monitor spatial index preparation state
- Log layer-added and layer-collection events
- Measure and report the completed load duration
- Clear loaded layers and reset progress state
