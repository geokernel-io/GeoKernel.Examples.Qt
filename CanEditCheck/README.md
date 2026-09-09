# CanEditCheck

CanEditCheck demonstrates how to inspect editing capabilities before modifying vector data in GeoKernel.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates an editable in-memory point layer. The application reports whether the layer can enter edit mode and whether the current selection can be edited or moved, updating the capability table as the user begins an edit session, selects features, commits changes, or rolls them back.

## GIS Workflow

This example shows how to:

- Create and populate an editable in-memory point layer
- Check whether a layer supports editing
- Start, commit, and roll back an edit session
- Select editable features on the map
- Check whether selected features can be edited
- Check whether selected features can be moved
- Update UI controls from current editing capabilities
- Reset the sample layer and refresh the map
