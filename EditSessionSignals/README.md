# EditSessionSignals

EditSessionSignals demonstrates how to observe vector layer edit session lifecycle signals in GeoKernel.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates an in-memory point layer for editing. The user can begin an edit session, add a feature, then commit or roll back the changes while the application records the corresponding session-started, committed, and rolled-back signals in a live event log.

## GIS Workflow

This example shows how to:

- Create an editable in-memory point layer
- Connect to edit session lifecycle signals
- Start a layer edit session
- Add a feature during an active session
- Observe the edit-session-started signal
- Commit changes and observe the committed signal
- Roll back changes and observe the rolled-back signal
- Display signal counts and event details
