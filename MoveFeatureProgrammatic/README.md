# MoveFeatureProgrammatic

MoveFeatureProgrammatic demonstrates how to reposition selected vector features from application code in GeoKernel.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates an editable in-memory point layer populated with attributed features. The user can select one or more points, choose a movement distance, and move the selection north, south, east, or west through the viewer editing API while the coordinate table updates immediately.

## GIS Workflow

This example shows how to:

- Create and populate an editable in-memory point layer
- Select one or more editable features
- Configure a movement delta
- Move selected features programmatically
- Apply horizontal and vertical coordinate offsets
- Synchronize updated geometry with a feature table
- Reset the layer and refresh the map
