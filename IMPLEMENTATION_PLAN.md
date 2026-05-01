# ClaudeUnrealMCP — Implementation Plan & Roadmap

**Created:** 2026-02-01
**Last Updated:** 2026-05-01
**Repo:** https://github.com/AgustinJimenez/ClaudeUnrealMCP

---

## Current Status: 87 tools

### Completed Sprints

| Sprint | Date | Features | Status |
|--------|------|----------|--------|
| 1 | 2026-02-01 | Blueprint function creation (create, add inputs/outputs, rename) | DONE |
| 2 | 2026-02-03 | Level actor properties (read/set actor props, components) | DONE |
| 3 | 2026-02-05 | Component map manipulation, replace component class | DONE |
| 4 | 2026-02-07 | CDO property manipulation, interface management | DONE |
| 5 | 2026-02-09 | Node manipulation (connect, disconnect, add struct nodes, delete) | DONE |
| 6 | 2026-02-11 | Input system reading (IMC) | DONE |
| 7 | 2026-02-13 | Struct migration (14 migration/fix tools) | DONE |
| 8 | 2026-02-15 | Enum migration, pin fixes | DONE |
| 9 | 2026-02-21 | Chooser table migration with nested walker | DONE |
| 10 | 2026-04-16 | Asset ops (duplicate, inspect, set_property) via C++ EditorAssetLibrary | DONE |

### Unique Strengths (no competitor has these)
- **14 BP-to-C++ migration tools** (struct/enum/interface/chooser migration)
- **Animation BP specialization** (clear_anim_graph, clear_animation_blueprint_tags)
- **Fine-grained node repair** (reconstruct_node, break_orphaned_pins, fix_pin_enum_type)
- **PropertyAccess path fixing**
- **Blueprint reparenting** with full tooling

---

## Competitive Gap Analysis (May 2026)

### Competitors
- **[StraySpark](https://www.strayspark.studio/products/unreal-mcp-server)** — commercial, 100+ tools
- **[remiphilippe/mcp-unreal](https://github.com/remiphilippe/mcp-unreal)** — open source, levels, procedural mesh
- **[GenOrca/unreal-mcp](https://github.com/GenOrca/unreal-mcp)** — open source, BT, widgets, materials

### Missing Features (Priority Order)

#### Sprint 11 — Actor & Level Management (HIGH priority, LOW effort)
- [ ] `spawn_actor` — spawn actor from class at location/rotation
- [ ] `destroy_actor` — remove actor from level
- [ ] `list_levels` — list all levels/sublevels in project
- [ ] `load_level` — open a level in editor
- [ ] `get_current_level` — get the currently open level path
- [ ] Wrap modifications in `GEditor->BeginTransaction()` / `EndTransaction()` for undo support

#### Sprint 12 — Material System (HIGH priority, MEDIUM effort)
- [ ] `create_material` — create new material asset
- [ ] `create_material_instance` — create material instance from parent
- [ ] `set_material_parameter` — set scalar/vector/texture params
- [ ] `list_material_parameters` — list available params on a material
- [ ] `assign_material_to_actor` — apply material to actor's mesh
- [ ] `read_material_graph` — inspect material expression nodes

#### Sprint 13 — Widget Blueprint / UMG (MEDIUM priority, MEDIUM effort)
- [ ] `create_widget_blueprint` — create UMG widget BP
- [ ] `add_widget` — add widget (Button, Text, Image, etc.) to canvas
- [ ] `set_widget_property` — set widget properties (text, color, size)
- [ ] `read_widget_tree` — inspect widget hierarchy

#### Sprint 14 — Behavior Tree (MEDIUM priority, MEDIUM effort)
- [ ] `create_behavior_tree` — create BT asset
- [ ] `create_blackboard` — create blackboard asset
- [ ] `add_blackboard_key` — add key to blackboard
- [ ] `add_bt_node` — add task/decorator/service to BT
- [ ] `connect_bt_nodes` — connect BT nodes

#### Sprint 15 — Quality of Life (MEDIUM priority, LOW effort)
- [ ] `undo` / `redo` — editor undo/redo
- [ ] `get_engine_version` — return UE version info
- [ ] `search_assets` — search assets by name/class/tag
- [ ] `get_asset_references` — find what references an asset
- [ ] `rename_asset` — rename/move asset
- [ ] `delete_asset` — delete asset with reference check

#### Sprint 16 — Static Mesh & Environment (LOWER priority)
- [ ] `create_static_mesh` — programmatic mesh creation
- [ ] `set_mesh_lod` — configure LOD settings
- [ ] `create_light` — add light actors to level
- [ ] `set_world_settings` — modify world settings

---

## Architecture Notes

### Adding a new command

1. Declare handler in `Source/ClaudeUnrealMCP/Public/MCPServer.h`
2. Register in `CommandHandlers` map in `MCPServerCore.cpp`
3. Implement in appropriate `MCPServer*.cpp` file
4. Add tool definition in `MCPServer/toolDefinitions.js`
5. Rebuild plugin, restart editor

### File organization
- `MCPServerCore.cpp` — command routing, ping, list_structs
- `MCPServerRead*.cpp` — read-only commands (blueprints, components, actors, etc.)
- `MCPServerComponent*.cpp` — component manipulation
- `MCPServerNode*.cpp` — blueprint graph node operations
- `MCPServerMigration*.cpp` — BP-to-C++ migration tools
- `MCPServerPython.cpp` — generic asset ops (duplicate, inspect, set_property)
- `MCPServerHelpers.cpp/.h` — shared utility functions

### Key constraints
- **DO NOT** add `PythonScriptPlugin` dependency — crashes editor on startup (PostLoad assertion)
- Use `UEditorAssetLibrary` (from `EditorScriptingUtilities` module) for asset ops instead
- All commands run on the game thread via `AsyncTask(ENamedThreads::GameThread, ...)`
- TCP server on port 9877, JSON protocol, newline-terminated messages
