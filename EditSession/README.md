# EditSession

EditSession demonstrates how to manage an editable vector layer in GeoKernel.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates an in-memory editable city point layer. The user can start an edit session, add temporary features, then either commit the changes or roll them back.

## GIS Workflow

This example shows how to:

- Create an editable in-memory vector layer
- Start and manage an edit session
- Add features during an active edit session
- Commit pending edits
- Roll back uncommitted changes
- Refresh the map after edit operations
