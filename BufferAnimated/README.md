# BufferAnimated

BufferAnimated demonstrates how to recalculate and animate a point buffer continuously with GeoKernel topology operations.

## Overview

It creates a source point entirely in memory and uses a `QTimer` to vary the buffer distance between minimum and maximum values. Each animation frame rebuilds the main polygon buffer and a secondary pulse ring, updates their styling, and reports the current frame, distance, result part count, and extent. Toolbar controls let the user pause the animation and adjust its update interval.

## GIS Workflow

This example shows how to:

- Create a point geometry in memory
- Generate polygon buffers with the topology engine
- Recalculate geometry on a timer
- Animate buffer distance between configured limits
- Render a secondary pulse buffer
- Update geometry styles for each frame
- Pause and resume the animation
- Adjust the animation interval at runtime
