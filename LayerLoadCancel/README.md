# LayerLoadCancel

LayerLoadCancel demonstrates how to cancel a long-running layer load operation in a GeoKernel viewer.

## Overview

It prepares a large one-million-point sample dataset, starts loading it with spatial index preparation enabled, and lets the user request cancellation while the layer is still being processed. Progress and status messages show the current loading/indexing stage, and the viewer only displays the layer if the load completes successfully.

## GIS Workflow

This example shows the basic workflow for:

- Initialize a map viewer
- Download and load a large vector dataset
- Configure cancellable layer loading
- Report loading and spatial index progress
- Cancel an in-progress layer load
