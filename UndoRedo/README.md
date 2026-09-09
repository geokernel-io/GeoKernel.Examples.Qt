# UndoRedo

UndoRedo demonstrates how to undo and redo edits on an active vector layer in GeoKernel.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates an editable in-memory point layer. Each point added by clicking the map becomes an undoable edit step, and the toolbar lets the user undo or redo one operation at a time or repeat the operation up to five times while the current history state is displayed in the side panel.

## GIS Workflow

This example shows how to:

- Create an editable in-memory point layer
- Add point features interactively
- Record layer edits in the undo history
- Check whether undo and redo operations are available
- Undo or redo a single edit step
- Apply multiple undo or redo operations
- Reset the edit history and refresh the map
