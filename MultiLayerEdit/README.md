# MultiLayerEdit

MultiLayerEdit demonstrates how to manage edit sessions across multiple vector layers in GeoKernel.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates two editable in-memory point layers with distinct styles. The user can switch the active edit layer, add features to either layer, and commit or roll back both edit sessions while the interface reports each layer's feature count, editing state, and dirty state.

## GIS Workflow

This example shows how to:

- Create multiple editable in-memory point layers
- Start simultaneous edit sessions on multiple layers
- Switch the active edit layer at runtime
- Add features to the active edit layer
- Monitor editing and dirty state per layer
- Commit changes across multiple layers
- Roll back changes across multiple layers
- Reset both layers and refresh the map
