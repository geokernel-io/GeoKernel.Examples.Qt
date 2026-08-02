# LabelCollisionOff

LabelCollisionOff demonstrates how label collision handling affects dense point labels in GeoKernel.

## Overview

It prepares the required world and city sample data, loads the same layers into two synchronized comparison views, and shows the difference between normal collision filtering and allowing label overlap. The left view keeps labels from colliding, while the right view disables collision avoidance so every city label can be drawn.

## GIS Workflow

This example shows how to:

- Enable labels on a point layer
- Configure labelAllowOverlap
- Compare collision-filtered and overlapping labels
- Style point labels with offset and halo settings
- Use side-by-side viewers for visual comparison
