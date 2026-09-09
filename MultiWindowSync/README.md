# MultiWindowSync

MultiWindowSync demonstrates how to keep multiple GeoKernel map viewers synchronized inside the same Qt application.

## Overview

It opens two viewer panes, loads the same world layer into both maps, and mirrors viewport changes between them so panning or zooming one viewer updates the other.

## GIS Workflow

This example shows:

- Creating multiple GisViewer instances
- Loading shared sample data
- Synchronizing visible map extents
- Enabling or disabling sync at runtime
- Applying the same navigation tools to both viewers
