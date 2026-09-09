# EditDirtyState

EditDirtyState demonstrates how to monitor pending changes on an editable vector layer in GeoKernel.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates an in-memory point layer for editing. The user can start an edit session, add a feature to make the layer dirty, then commit or roll back the changes while the interface displays the current editing state, dirty flag, feature count, and emitted edit-state signals.

## GIS Workflow

This example shows how to:

- Create an editable in-memory point layer
- Start a layer edit session
- Check the current layer dirty state
- Add a feature as a pending edit
- Observe edit-state change signals
- Commit pending layer changes
- Roll back uncommitted edits
- Refresh the displayed state after each operation
