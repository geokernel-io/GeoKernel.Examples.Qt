# SetAttributes

SetAttributes demonstrates how to update feature attribute values on an editable vector layer in GeoKernel.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates an editable in-memory point layer with Name, Status, and Priority fields. The user can select a point, edit its values in a form, apply the attributes through the viewer editing API, inspect the synchronized attribute table, and undo or redo changes.

## GIS Workflow

This example shows how to:

- Create an editable in-memory layer with custom attribute fields
- Populate features with initial attribute values
- Select a feature on the map or from a table
- Read feature attributes into an editor form
- Update feature attributes through the editing API
- Synchronize edited values with an attribute table
- Undo, redo, and reset attribute changes
