# SmithUE Tools Reference

> Generated from `/api/v1/tools`. Total: **314 tools** across **29 domains**.

---

## AI

### `create_blackboard`

Create a Blackboard Data asset (UBlackboardData). Add keys with blackboard_add_key; read with read_blackboard.

**Parameters:**

- `name` (string, required): Asset name (convention: BB_*)
- `path` (string, required): Content folder, e.g. /Game/AI

### `blackboard_add_key`

Add a typed key to a Blackboard Data asset. MUTATES the asset; call save_asset to persist.

**Parameters:**

- `blackboard_path` (string, required): Blackboard asset path
- `key_name` (string, required): Key name
- `key_type` (string, required): Key type

### `blackboard_remove_key`

Remove a key (by name) from a Blackboard Data asset. MUTATES; call save_asset to persist.

**Parameters:**

- `blackboard_path` (string, required): Blackboard asset path
- `key_name` (string, required): Key name to remove

### `read_blackboard`

Read a Blackboard Data asset: its keys (name + type) and parent. Read-only.

**Parameters:**

- `blackboard_path` (string, required): Blackboard asset path

### `create_behavior_tree`

Create a Behavior Tree asset (UBehaviorTree), optionally linking a Blackboard. Open it in the editor to author the node graph (root/composites/tasks).

**Parameters:**

- `name` (string, required): Asset name (convention: BT_*)
- `path` (string, required): Content folder
- `blackboard_path` (string): Optional Blackboard asset to link

### `bt_set_blackboard`

Set/replace the Blackboard asset linked to a Behavior Tree. MUTATES the asset.

**Parameters:**

- `bt_path` (string, required): Behavior Tree asset path
- `blackboard_path` (string, required): Blackboard asset path

### `read_behavior_tree`

Read a Behavior Tree asset: linked blackboard + root node class. Read-only.

**Parameters:**

- `bt_path` (string, required): Behavior Tree asset path

### `bt_add_node`

Add a composite or task node to a Behavior Tree and connect it under the root (or a parent composite node index). node_type: 'Selector'/'Sequence'/'SimpleParallel' (composite) or a task name ('Wait','MoveTo','RunEQSQuery',... resolves UBTTask_* by name). Builds the editor graph + rebuilds the runtime tree. MUTATES; call save_asset to persist. Returns the new node's graph index (use as parent for further nodes).

**Parameters:**

- `bt_path` (string, required): Behavior Tree asset path
- `node_type` (string, required): Selector/Sequence/SimpleParallel or a task name (Wait, MoveTo, ...)
- `parent` (string): Parent composite node index (default: root). Tasks cannot be parents.

### `bt_read_node`

Read a Behavior Tree graph node's runtime instance: class + editable properties (name/type/value). node_index from bt_add_node / the graph. Read-only.

**Parameters:**

- `bt_path` (string, required): Behavior Tree asset path
- `node_index` (int, required): Graph node index

### `bt_set_node_property`

Set a property on a Behavior Tree node's runtime instance (e.g. a Wait task's WaitTime, a MoveTo's AcceptableRadius). Use bt_read_node to list editable properties. MUTATES; call save_asset to persist.

**Parameters:**

- `bt_path` (string, required): Behavior Tree asset path
- `node_index` (int, required): Graph node index (from bt_add_node)
- `property` (string, required): Property name on the node (see bt_read_node)
- `value` (string, required): Value as string (property-reflection import, e.g. '2.5', 'true')

### `bt_add_decorator`

Attach a decorator (conditional) to a Behavior Tree node. decorator = fuzzy UBTDecorator subclass name (Blackboard, CompareBBEntries, Cooldown, TimeLimit, Loop, ...). node_index from bt_add_node. MUTATES; call save_asset.

**Parameters:**

- `bt_path` (string, required): Behavior Tree asset path
- `node_index` (int, required): Graph node index to attach the decorator to
- `decorator` (string, required): Decorator class (fuzzy), e.g. Blackboard / Cooldown / Loop

### `bt_add_service`

Attach a service (periodic tick) to a Behavior Tree composite node. service = fuzzy UBTService subclass name (DefaultFocus, RunEQS, BlueprintBase, ...). node_index must be a composite. MUTATES; call save_asset.

**Parameters:**

- `bt_path` (string, required): Behavior Tree asset path
- `node_index` (int, required): Composite graph node index to attach the service to
- `service` (string, required): Service class (fuzzy), e.g. DefaultFocus / RunEQS

### `bt_remove_node`

Remove a node (task/composite) from a Behavior Tree graph by index, break its links, and rebuild the runtime tree. Cannot remove the Root node. node_index from bt_add_node / the graph. MUTATES; call save_asset.

**Parameters:**

- `bt_path` (string, required): Behavior Tree asset path
- `node_index` (int, required): Graph node index to remove

### `create_eqs`

Create an Environment Query (EQS) asset (UEnvQuery). Open it in the editor to add generators/tests.

**Parameters:**

- `name` (string, required): Asset name (convention: EQS_*)
- `path` (string, required): Content folder

### `read_eqs`

Read an EQS asset: option/generator count. Read-only.

**Parameters:**

- `eqs_path` (string, required): EQS asset path

### `eqs_add_option`

Add a generator option to an EQS asset (builds the editor graph + rebuilds runtime Options). generator = fuzzy name of a UEnvQueryGenerator subclass (SimpleGrid, ActorsOfClass, OnCircle, Donut, Cone, CurrentLocation, ...). MUTATES; call save_asset to persist.

**Parameters:**

- `eqs_path` (string, required): EQS asset path
- `generator` (string, required): Generator class (fuzzy), e.g. SimpleGrid / ActorsOfClass / OnCircle

### `eqs_add_test`

Add a scoring/filtering test to an EQS option (generator). test = fuzzy UEnvQueryTest subclass name (Distance, Dot, Trace, Pathfinding, Overlap, Project, ...). option = 0-based index among the option/generator nodes (default: last added). MUTATES; call save_asset.

**Parameters:**

- `eqs_path` (string, required): EQS asset path
- `test` (string, required): Test class (fuzzy), e.g. Distance / Dot / Trace / Pathfinding
- `option` (int): Option index to add the test to (default: last)

### `eqs_remove_option`

Remove a generator option (and its tests) from an EQS asset by 0-based option index, then rebuild the runtime Options. MUTATES; call save_asset.

**Parameters:**

- `eqs_path` (string, required): EQS asset path
- `option` (int, required): Option index to remove (from read_eqs order)

### `create_state_tree`

Create a State Tree asset (UStateTree) with the StateTree Component schema, a root state, and an initial compile. Open it in the editor to author states/tasks/transitions.

**Parameters:**

- `name` (string, required): Asset name (convention: ST_*)
- `path` (string, required): Content folder

### `read_state_tree`

Read a State Tree asset: schema + root/sub states. Read-only.

**Parameters:**

- `state_tree_path` (string, required): State Tree asset path

### `state_tree_add_state`

Add a child state to a State Tree (under the given parent state name, or the first root if omitted), then recompile. MUTATES + recompiles; call save_asset to persist.

**Parameters:**

- `state_tree_path` (string, required): State Tree asset path
- `state_name` (string, required): New state name
- `parent` (string): Parent state name (default: first root state)

### `state_tree_add_transition`

Add a transition from one state to another (GotoState) and recompile. from_state/to_state are state names. trigger: OnStateCompleted (default) / OnStateSucceeded / OnStateFailed. MUTATES + recompiles; call save_asset to persist.

**Parameters:**

- `state_tree_path` (string, required): State Tree asset path
- `from_state` (string, required): Source state name
- `to_state` (string, required): Target state name
- `trigger` (string): Transition trigger (default OnStateCompleted)

### `state_tree_remove_state`

Remove a state (by name, with its child states) from a State Tree and recompile. Cannot remove the last remaining root state. MUTATES + recompiles; call save_asset.

**Parameters:**

- `state_tree_path` (string, required): State Tree asset path
- `state_name` (string, required): State name to remove

## Analysis

### `analyze_module`

Analyze C++ source files in a directory and return a nomnoml relationship diagram (class inheritance, composition, includes). Supports large projects via directory-level scoping.

**Parameters:**

- `path` (string, required): Absolute or project-relative path to source directory (e.g. Plugins/SmithUE/Source/SmithUE)
- `depth` (string): Analysis depth: 'module' (file-level overview) or 'class' (full class relationships). Default: class
- `max_files` (number): Max files to analyze (default: 200, prevents timeout on huge directories)

### `analyze_dependencies`

Analyze Build.cs module dependencies and return a nomnoml dependency graph.

**Parameters:**

- `path` (string, required): Path to directory containing .Build.cs files (or single .Build.cs path). Searches recursively.

### `analyze_blueprints`

Analyze Blueprint assets in a Content Browser path and return a nomnoml inheritance + component composition diagram.

**Parameters:**

- `content_path` (string, required): Content Browser path (e.g. /Game/Blueprints)
- `recursive` (boolean): Search subdirectories (default: true)

### `bp_get_compile_errors`

Compile a Blueprint and return compiler errors and warnings

**Parameters:**

- `bp_path` (string, required): Blueprint asset path

### `bp_refresh_all_nodes`

Reconstruct all nodes in a Blueprint

**Parameters:**

- `bp_path` (string, required): Blueprint asset path

### `bp_find_unconnected_pins`

Find unconnected Blueprint exec and data pins

**Parameters:**

- `bp_path` (string, required): Blueprint asset path

### `bp_fix_broken_references`

Remove non-existent Blueprint variable references and refresh nodes

**Parameters:**

- `bp_path` (string, required): Blueprint asset path

### `asset_get_references`

Get package dependencies for an asset

**Parameters:**

- `asset_path` (string, required): Asset package path

### `asset_get_referencers`

Get packages that reference an asset

**Parameters:**

- `asset_path` (string, required): Asset package path

### `asset_find_orphans`

Find assets in a folder with no referencers

**Parameters:**

- `folder_path` (string): Content folder path (default: /Game)
- `include_types` (array): Optional array of asset type names

### `asset_get_dependency_tree`

Get a recursive asset dependency tree

**Parameters:**

- `asset_path` (string, required): Asset package path
- `depth` (int): Maximum dependency depth (default: 2)

### `asset_validate`

Validate that asset paths resolve and load without errors

**Parameters:**

- `asset_paths` (array, required): Array of asset package paths

### `find_broken_assets`

Scan a content folder for BROKEN assets: (1) missing hard references — the asset depends on a /Game package that no longer exists (dangling ref that will null/error at runtime), and (2) redirectors — stale ObjectRedirector assets that should be fixed up. Fast: uses the Asset Registry graph (no asset loading) unless load_check=true, which additionally loads each candidate to catch packages that fail to load. Scope with folder_path; cap results with max. Returns the broken asset list with per-asset reasons + the specific missing packages.

**Parameters:**

- `folder_path` (string): Content folder to scan (default: /Game)
- `load_check` (bool): Also attempt to load each asset to catch load failures (slower). Default false.
- `max` (int): Max broken assets to return (default 200)

### `map_check_errors`

Run map check on the active editor world

## Animation

### `anim_create_montage`

Create an AnimMontage asset for a skeleton

**Parameters:**

- `name` (string, required): Montage asset name
- `path` (string, required): Content folder path
- `skeleton_path` (string, required): Skeleton asset path

### `anim_read_montage`

Read montage sections, notifies, and slots

**Parameters:**

- `montage_path` (string, required): AnimMontage asset path

### `anim_add_section`

Add a section to an AnimMontage

**Parameters:**

- `montage_path` (string, required): AnimMontage asset path
- `section_name` (string, required): Section name
- `start_time` (float, required): Section start time in seconds

### `anim_link_sections`

Link two montage sections for sequential playback

**Parameters:**

- `montage_path` (string, required): AnimMontage asset path
- `from_section` (string, required): Source section name
- `to_section` (string, required): Target section name

### `anim_add_notify`

Add an anim notify at a time

**Parameters:**

- `montage_path` (string, required): AnimMontage asset path
- `notify_name` (string, required): Notify display name
- `time` (float, required): Time in seconds
- `notify_class` (string): Optional AnimNotify class path

### `anim_create_blueprint`

Create an AnimBlueprint asset

**Parameters:**

- `name` (string, required): AnimBP asset name
- `path` (string, required): Content folder path
- `skeleton_path` (string, required): Skeleton asset path
- `parent_class` (string): Optional parent AnimInstance class path

### `anim_read_blueprint`

Read AnimBP info (state machines, variables, skeleton)

**Parameters:**

- `anim_bp_path` (string, required): AnimBlueprint asset path

## Asset

### `list_assets`

List assets in a content folder

**Parameters:**

- `folder_path` (string, required): Content folder path (e.g. /Game)
- `type_filter` (string): Optional: filter by asset type name (e.g. StaticMesh)

### `find_asset`

Find assets by name wildcard pattern

**Parameters:**

- `name_pattern` (string, required): Wildcard pattern to match asset names (e.g. *Weapon*)
- `asset_type` (string): Optional: filter by asset type name

### `get_asset_info`

Get detailed information about a specific asset

**Parameters:**

- `asset_path` (string, required): Full asset path (e.g. /Game/Materials/M_Base)

### `rename_asset`

Rename an asset to a new name within the same folder

**Parameters:**

- `asset_path` (string, required): Full asset path to rename
- `new_name` (string, required): New asset name (without path)

### `duplicate_asset`

Duplicate an asset to a new path

**Parameters:**

- `source_path` (string, required): Source asset path
- `dest_path` (string, required): Destination asset path (full path including new name)

### `delete_asset`

Delete an asset. Checks references first; returns referencers if found. force=true force-deletes even with in-memory referencers, nulling them.

**Parameters:**

- `asset_path` (string, required): Full asset path to delete (e.g. /Game/Materials/M_Old)
- `force` (boolean): Force delete even if references exist (default: false)

### `move_asset`

Move an asset to a new path (different folder and/or name). Updates all references.

**Parameters:**

- `asset_path` (string, required): Current full asset path
- `new_path` (string, required): New full asset path (e.g. /Game/NewFolder/NewName)

### `move_folder`

Move all assets under a content folder (recursive) to another mount point/folder, preserving relative structure. Uses batched IAssetTools::RenameAssets, which updates all references and leaves redirectors. Supports dry_run.

**Parameters:**

- `source_folder` (string, required): Source content folder (e.g. /Game/UltraDynamicSky)
- `dest_folder` (string, required): Destination content folder (e.g. /UltraDynamicSky). Mount point must exist.
- `dry_run` (boolean): Preview the rename plan without applying (default: false)
- `save` (boolean): Save all dirty packages after moving (default: true)

### `get_dependency_closure`

Compute the full recursive dependency closure of one or more root assets, restricted to a content prefix (default /Game). Returns every dependency package plus a 'shared' flag for those still referenced by packages OUTSIDE the closure (i.e. would break other content if migrated). Read-only. Use before migrating a map's dependencies into a plugin.

**Parameters:**

- `root_assets` (array, required): Root asset/object paths (e.g. ["/Plugin/Maps/MyLevel"])
- `content_prefix` (string): Only follow/collect dependencies under this prefix (default /Game)
- `max_list` (number): Cap the returned package/shared lists (counts are always exact; default 1000)

### `move_paths`

Batch-move an explicit list of asset packages to a destination root, remapping strip_prefix->dest_root (e.g. /Game/Building/X -> /SOBSJunction3/Building/X). Uses one batched IAssetTools::RenameAssets (updates references, leaves redirectors). Pair with get_dependency_closure to migrate a map's whole dependency set. Batch renames of Blueprints can raise CDO-reference confirm modals: arm set_dialog_auto_response mode=confirm first. Supports dry_run.

**Parameters:**

- `paths` (array, required): Asset/package paths to move (e.g. ["/Game/Building/SM_X"])
- `dest_root` (string, required): Destination mount/root (e.g. /SOBSJunction3). Must be a mounted content root.
- `strip_prefix` (string): Prefix stripped from each source path before prepending dest_root (default /Game)
- `dry_run` (boolean): Preview the remap plan without applying (default false)
- `save` (boolean): Save all dirty packages after moving (default true)

### `fixup_redirectors`

Clean up ObjectRedirectors under a folder (recursive). SAFE DEFAULT: only deletes redirectors that have NO external referencers (the normal state after a folder migration, since references were already rewritten) - this avoids AssetTools::FixupReferencers, which ASSERTS/crashes on World & Blueprint-class/CDO redirectors left by migrated .umap packages. Redirectors that still have referencers are reported (not touched) unless force_fixup=true.

**Parameters:**

- `folder_path` (string, required): Content folder to scan for redirectors (e.g. /Game)
- `force_fixup` (boolean): Run AssetTools::FixupReferencers over still-referenced redirectors (rewrites+saves referencers). Default false. UNSAFE for World/Blueprint redirectors - can crash the editor.

### `resolve_redirectors`

Resolve ObjectRedirectors under a folder by REWRITING every referencer (hard AND soft references) to point at the redirector's real target, then deleting the redirector. Uses ObjectTools::ConsolidateObjects per redirector - a DIFFERENT engine code path than fixup_redirectors/AssetTools::FixupReferencers (which asserts/crashes on some projects in UE5.8). Handles the soft-reference redirectors left after batch-migrating a map's dependencies. Saves modified packages.

**Parameters:**

- `folder_path` (string, required): Content folder to scan for redirectors (e.g. /Game)
- `max_resolve` (number): Optional cap on how many redirectors to resolve this call (for batching very large sets; default: all)
- `skip_poison` (boolean): Skip redirectors whose target is a World/level or Blueprint-generated class/CDO (ConsolidateObjects crashes on these in UE5.8). Default TRUE — they are reported in 'skipped_poison' instead of crashing the editor; use redirect_references + resave_packages for those.
- `save` (boolean): Save modified packages after resolving (default true)

### `resave_packages`

Force-load and re-save every package under a folder (or an explicit list),固化引用: hard/soft references are rewritten to the CURRENT real path of the object they resolve to (following redirectors). This is the in-editor equivalent of the 'ResavePackages -fixupredirects' commandlet, but crucially WORKS ON PLUGIN packages (the commandlet's -projectonly / default scope skips plugins). Use after moving assets to固化 references held by plugin assets / FoliageTypes / levels so their /Game redirectors become unreferenced and deletable.

**Parameters:**

- `folder_path` (string): Content folder to resave recursively (e.g. /MyPlugin). Provide this OR paths.
- `paths` (array): Explicit package/asset paths to resave. Provide this OR folder_path.
- `skip_maps` (boolean): Skip World/level packages (default false). Levels are slower and may need the level to be opened; set true to resave only content assets.
- `max` (number): Optional cap on packages processed this call (for batching very large sets)

### `redirect_references`

Explicitly rewrite references (hard AND soft) inside a set of referencer assets: every reference to from_asset is replaced with to_asset via FArchiveReplaceObjectRef, then the referencers are saved. Does NOT use AssetTools::FixupReferencers / ObjectTools::ConsolidateObjects (both crash on World/Blueprint/CDO redirectors in UE5.8). Use to surgically re-point stubborn references (e.g. a level actor still pointing at a /Game BP redirector) at the real asset in the plugin.

**Parameters:**

- `referencers` (array, required): Asset paths whose references should be rewritten (e.g. the level / FoliageType / material holding the stale ref)
- `from_asset` (string, required): The asset currently referenced (usually a redirector or old path)
- `to_asset` (string, required): The real asset the references should point at
- `save` (boolean): Save modified referencers afterwards (default true)

### `plan_migration`

DRY-RUN migration preview. Given a source folder + destination root remap, reports BEFORE moving anything: how many packages would move, how many redirectors that leaves, and — most importantly — classifies each mover's external referencers as auto-fixable (plain asset refs) vs STUBBORN (World/level actor refs & Blueprint-class refs that the engine cannot auto-固化, i.e. will leave hard-to-clean redirectors). Also lists shared assets (referenced from outside the move set). Lets you see the redirector/breakage cost before committing.

**Parameters:**

- `source_folder` (string, required): Folder whose assets would be moved (e.g. /MyPlugin/Building)
- `dest_root` (string, required): Destination root (e.g. /MyPlugin/Shared)
- `strip_prefix` (string): Prefix stripped before prepending dest_root (default = source_folder's mount, e.g. /MyPlugin)
- `max_list` (number): Cap on sample lists returned (counts are exact; default 50)

### `list_redirectors`

Diagnose ObjectRedirectors under a folder: classifies each by referencer origin (none = safe to delete; plugin/game refs = need resave_packages/redirect_references to固化 first) and by kind (asset / World / Blueprint-class / CDO — the last two are ConsolidateObjects 'poison'). Read-only. Use to pick the right cleanup path before touching anything.

**Parameters:**

- `folder_path` (string, required): Content folder to scan (e.g. /MyPlugin)
- `max_list` (number): Cap on per-bucket sample lists (counts are exact; default 30)

### `move_folders`

Batch several folder moves in ONE call (each {source, dest}), instead of calling move_folder N times. Uses a single batched IAssetTools::RenameAssets across all mappings so cross-folder references between the moved sets are fixed up together (fewer leftover redirectors than sequential move_folder). Supports dry_run.

**Parameters:**

- `mappings` (array, required): Array of {"source":"/P/A","dest":"/P/Shared/A"} objects
- `dry_run` (boolean): Preview only (default false)
- `save` (boolean): Save dirty packages afterwards (default true)

### `delete_empty_folders`

Delete empty content folders (no assets in the folder or any subfolder) on disk under a root, recursively bottom-up. Safe: empty folders hold nothing and break nothing. Use to tidy up the source tree left behind after migrating assets out.

**Parameters:**

- `root` (string, required): Content root to clean (e.g. /Game or /MyPlugin)
- `dry_run` (boolean): List what would be deleted without deleting (default false)

### `get_job_status`

Report live progress of the current long-running batch command (resave_packages / move_folder(s) / fixup_redirectors / resolve_redirectors). WORKER-SAFE: responds even while the game thread is busy inside the batch, so you can poll真实进度 instead of grepping the log. Returns active/operation/processed/total/percent/current_item/elapsed_seconds.

### `consolidate_assets`

Consolidate assets: replace all references to assets_to_merge with asset_to_keep, then delete the merged assets (same as Content Browser 'Replace References'). All assets must be of compatible classes.

**Parameters:**

- `asset_to_keep` (string, required): Asset path that references will point to after consolidation
- `assets_to_merge` (array, required): Array of asset paths to be replaced by asset_to_keep and deleted

### `asset_editor`

Open or close asset editors. Supports single or multiple assets.

**Parameters:**

- `action` (string, required): 'open' or 'close'
- `asset_paths` (array, required): Array of asset paths (e.g. ["/Game/Materials/M_A", "/Game/Materials/M_B"])

### `set_asset_property`

Set any property on a loaded UObject asset (Texture2D, StaticMesh, SkeletalMesh, Material, etc.) by dotted property path. Use for texture compression, LOD settings, mesh properties, etc.

**Parameters:**

- `asset_path` (string, required): Full asset path (e.g. /Game/BP/T_Wood_Normal.T_Wood_Normal)
- `property_path` (string, required): Dotted property path (e.g. CompressionSettings, SRGB, LightMapResolution, BodySetup.CollisionTraceFlag)
- `value` (string, required): Value to set (string representation; enums use name like TC_Normalmap)
- `save` (boolean): Auto-save the asset after modification (default: true)

### `save_asset`

Save a single asset to disk

**Parameters:**

- `asset_path` (string, required): Full asset path to save (e.g. /Game/Materials/M_Base)

### `save_all_dirty`

Save all dirty (modified) assets to disk

### `get_content_browser_selection`

Get the folders and assets currently selected in the Content Browser

### `sync_content_browser`

Navigate the Content Browser to a folder or asset and bring it to focus

**Parameters:**

- `folder_path` (string): Content folder to navigate to (e.g. /Game/Materials)
- `asset_path` (string): Asset to select and reveal (e.g. /Game/Materials/M_Base)

### `capture_asset_thumbnail`

Render any /Game asset's thumbnail (material, texture, static/skeletal mesh, blueprint, particle, etc.) to a PNG file WITHOUT opening its editor. Uses the engine's thumbnail renderer, so it reflects the asset's real appearance. Size is a moderate square (default 256, clamped 64-1024) intended for AI visual inspection — avoid huge sizes. Great for visually verifying material/texture edits.

**Parameters:**

- `asset_path` (string, required): Asset object path, e.g. /Game/Materials/M_Rock
- `path` (string, required): Full file path for the PNG output (e.g. C:/temp/thumb.png)
- `size` (number): Square thumbnail size in px (default 256, clamped 64-1024)

### `generate_texture`

Generate a texture from a text prompt using an AI image generation API. Uses Pollinations.ai (free, no API key) by default. Provide endpoint + api_key for DALL-E, Imagen, or other providers. Returns a task_id for polling.

**Parameters:**

- `prompt` (string, required): Text prompt describing the desired texture
- `endpoint` (string): AI API endpoint URL (default: https://image.pollinations.ai/; auto-detects Pollinations/DALL-E/Imagen/OpenAI/Google format)
- `api_key` (string): API authentication key (not needed for Pollinations.ai)
- `save_path` (string): Content Browser path for the asset (default: /Game/GeneratedTextures)
- `asset_name` (string): Custom asset name (default: auto-generated timestamp)
- `model` (string): Model name to pass to the API (e.g. dall-e-3, imagen-4.0)
- `aspect_ratio` (string): Image size/ratio (e.g. 1024x1024, 16:9)
- `create_material` (boolean): Auto-create a material with this texture (default: false)
- `texture_type` (string): Texture type for auto-setting compression + SRGB after import: Diffuse (default), Normal, AO, Roughness, Metallic, Height, Mask. Normal sets TC_Normalmap+SRGB=false; AO/Roughness/Metallic/Mask set TC_Masks+SRGB=false; Height sets TC_Displacementmap+SRGB=false.

### `generate_audio`

Generate a sound asset (USoundWave) from text using Pollinations.ai TTS. Requires a Pollinations API key (free at pollinations.ai). Set it in Project Settings > Plugins > SmithUE, or pass api_key directly.

**Parameters:**

- `text` (string, required): Text to synthesize into speech/audio
- `api_key` (string): Pollinations.ai API key. If omitted, reads from Project Settings > SmithUE > Pollinations Audio API Key.
- `voice` (string): Voice name: alloy, echo, fable, onyx, nova (default), shimmer
- `model` (string): TTS model (default: openai-tts)
- `save_path` (string): Content Browser path for the asset (default: /Game/GeneratedAudio)
- `asset_name` (string): Custom asset name (default: auto-generated)

### `check_generation_task`

Check the status of an asynchronous texture generation task

**Parameters:**

- `task_id` (string, required): Task ID returned by generate_texture

### `get_asset_property`

Read a property value from a loaded asset by dotted path (e.g. LightmapCoordinateIndex, StaticMaterials[0]). Asset must be loaded (is_loaded=true).

**Parameters:**

- `asset_path` (string, required): Asset content path, e.g. /Game/Meshes/SM_Crate
- `property` (string, required): Dotted property path, e.g. LightmapCoordinateIndex

### `scan_assets`

Folder-scoped asset scan. Returns v1 linter metadata: naming, path, parent class, material slots, LOD count, collision presence.

**Parameters:**

- `folder_path` (string, required): UE content path, e.g. /Game/MyProject
- `recursive` (boolean): Recurse into sub-folders. Default: false
- `class_filter` (array): Class name filter array, e.g. ["Blueprint"]. Empty = all assets.

## Blueprint

### `bp_create`

Create a new Blueprint asset (asset-level: creates a new Blueprint ASSET at a package path; for adding a node inside a graph use bp_create_node).

**Parameters:**

- `name` (string, required): Blueprint asset name
- `parent_class` (string, required): Parent class name
- `save_path` (string, required): Destination content path

### `create_blueprint_interface`

Create a Blueprint Interface asset (UInterface, BPTYPE_Interface). Add interface functions with bp_add_function; implement it on a Blueprint with bp_implement_interface.

**Parameters:**

- `name` (string, required): Interface asset name (convention: BPI_*)
- `save_path` (string, required): Destination content path

### `bp_implement_interface`

Add (implement) an interface on a Blueprint. interface_path = a Blueprint-interface asset path (e.g. /Game/BPI_Foo) or a native UInterface class name. Compiles the Blueprint afterwards.

**Parameters:**

- `bp_path` (string, required): Target Blueprint path
- `interface_path` (string, required): Interface asset path or native UInterface class name

### `bp_add_function`

Add a function graph to a Blueprint

**Parameters:**

- `bp_path` (string, required): Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints
- `function_name` (string, required): New function name
- `inputs` (array): Optional input pin definitions
- `outputs` (array): Optional output pin definitions

### `bp_create_node`

Create a node inside a Blueprint graph (in-graph: adds a node inside a Blueprint graph; for a new Blueprint ASSET use bp_create). Returns node ids that become stale after any graph mutation — re-run bp_describe_graph before reusing them.

**Parameters:**

- `bp_path` (string, required): Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints
- `graph_name` (string, required): Target graph name
- `node_class` (string, required): Node class name
- `position` (object): Optional {x,y} node position
- `function_name` (string): Function name or 'ClassName::FunctionName' for K2Node_CallFunction nodes. Omit for self-configuring subclasses that already set their own function (e.g. K2Node_GetEditorProperty).
- `variable_name` (string): Variable name for K2Node_VariableGet or K2Node_VariableSet nodes
- `macro_path` (string): Macro graph asset path for K2Node_MacroInstance nodes
- `key` (string): Input key name (e.g. 'W', 'Gamepad_LeftX') for K2Node_InputKey nodes
- `input_action` (string): InputAction asset path for K2Node_EnhancedInputAction nodes
- `target_class` (string): Target class for K2Node_DynamicCast nodes. Accepts a native class name or a /Game/... Blueprint asset path (resolves the generated _C class).
- `owner_class` (string): Optional owner class for K2Node_VariableGet/VariableSet when the variable belongs to a FOREIGN class (e.g. a variable on a Cast result). Accepts a native class name or a /Game/... Blueprint asset path. Omit to resolve against the current Blueprint (Self).

### `bp_connect_pins`

Connect two Blueprint node pins (adds a wire between two pins; to remove an existing wire use bp_disconnect_pins).

**Parameters:**

- `bp_path` (string, required): Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints
- `graph_name` (string, required): Target graph name
- `source_node_id` (string, required): Source node GUID
- `source_pin` (string, required): Source pin name
- `target_node_id` (string, required): Target node GUID
- `target_pin` (string, required): Target pin name

### `bp_disconnect_pins`

Disconnect two Blueprint node pins (removes an existing wire between two pins; to add a new wire use bp_connect_pins).

**Parameters:**

- `bp_path` (string, required): Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints
- `graph_name` (string, required): Target graph name
- `source_node_id` (string, required): Source node GUID or N-id
- `source_pin` (string, required): Source pin name
- `target_node_id` (string, required): Target node GUID or N-id
- `target_pin` (string, required): Target pin name

### `bp_set_pin_default`

Set a Blueprint node pin default value

**Parameters:**

- `bp_path` (string, required): Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints
- `graph_name` (string, required): Target graph name
- `node_id` (string, required): Node GUID
- `pin_name` (string, required): Pin name
- `value` (string, required): Default value string

### `bp_set_anim_node_property`

MUTATES an AnimGraph UAnimGraphNode only: set an internal FAnimNode struct property by reflection (e.g. Sequence or PlayRate). Not for regular K2 nodes or state machines; use bp_read_anim_node first for valid property names.

**Parameters:**

- `bp_path` (string, required): AnimBlueprint asset path
- `graph_name` (string, required): AnimGraph graph name as returned by bp_describe_graph/anim_read_blueprint
- `node_id` (string, required): AnimGraph node GUID or fresh N-id
- `property` (string, required): Internal FAnimNode property name; run bp_read_anim_node to list valid names
- `value` (string, required): ImportText value string, e.g. 1.0, True, or an asset reference

### `bp_expose_anim_pin`

MUTATES an AnimGraph UAnimGraphNode only: show or hide one optional internal FAnimNode property pin. Not for regular Blueprint pins; ids can go stale after mutation — re-run bp_describe_graph/bp_read_anim_node.

**Parameters:**

- `bp_path` (string, required): AnimBlueprint asset path
- `graph_name` (string, required): AnimGraph graph name
- `node_id` (string, required): AnimGraph node GUID or fresh N-id
- `property` (string, required): Optional FAnimNode property pin name; run bp_read_anim_node to list valid names
- `show` (boolean, required): true to expose the property as a pin; false to hide it

### `bp_bind_anim_property`

MUTATES an AnimGraph UAnimGraphNode only: bind one anim node property to a MEMBER VARIABLE via PropertyBindings fast-path (no wire). variable must be a member variable name; empty variable unbinds. Not for functions, external objects, or state machines.

**Parameters:**

- `bp_path` (string, required): AnimBlueprint asset path
- `graph_name` (string, required): AnimGraph graph name
- `node_id` (string, required): AnimGraph node GUID or fresh N-id
- `property` (string, required): Bindable internal FAnimNode property name, e.g. PlayRate; run bp_read_anim_node first
- `variable` (string, required): Member variable name to bind; empty string removes existing binding

### `bp_read_anim_node`

READ-ONLY: inspect one AnimGraph UAnimGraphNode's internal FAnimNode settable properties, optional exposed pins, and PropertyBindings. Use before bp_set_anim_node_property/bp_expose_anim_pin/bp_bind_anim_property; not for regular K2 nodes or state machines.

**Parameters:**

- `bp_path` (string, required): AnimBlueprint asset path
- `graph_name` (string, required): AnimGraph graph name
- `node_id` (string, required): AnimGraph node GUID or fresh N-id

### `bp_add_state_machine`

CREATE: AnimBlueprint AnimGraph state machines ONLY. Adds a UAnimGraphNode_StateMachine to an AnimGraph and returns node_id plus state_machine_graph for follow-up bp_add_anim_state; ids go stale after graph mutation — re-run bp_describe_graph/bp_read_state_machine.

**Parameters:**

- `bp_path` (string, required): AnimBlueprint asset path
- `graph_name` (string, required): Target AnimGraph graph name (usually AnimGraph)
- `position` (object): Optional {x,y} node position

### `bp_add_anim_state`

CREATE: AnimBlueprint state-machine graphs ONLY. Adds a UAnimStateNode with its UAnimationStateGraph BoundGraph; state_machine accepts state-machine node_id or graph name. First state is wired from Entry. Returns bound_graph for bp_create_node/bp_set_anim_node_property population; ids go stale.

**Parameters:**

- `bp_path` (string, required): AnimBlueprint asset path
- `state_machine` (string, required): State machine node GUID/N-id or state machine graph name returned by bp_add_state_machine
- `state_name` (string, required): State name / BoundGraph rename suggestion
- `position` (object): Optional {x,y} state node position

### `bp_add_anim_transition`

CREATE: AnimBlueprint state-machine graphs ONLY. Adds a UAnimStateTransitionNode between two states with its UAnimationTransitionGraph rule BoundGraph. from_state/to_state accept state node_id or state name. Returns rule_graph for condition nodes; ids go stale.

**Parameters:**

- `bp_path` (string, required): AnimBlueprint asset path
- `state_machine` (string, required): State machine node GUID/N-id or state machine graph name
- `from_state` (string, required): Source state node GUID/N-id or state name
- `to_state` (string, required): Target state node GUID/N-id or state name
- `position` (object): Optional {x,y} transition node position

### `bp_read_state_machine`

READ-ONLY pair for state-machine create tools: AnimBlueprint state machines ONLY. Reports states, transitions, Entry target, and every state/transition BoundGraph name; state_machine accepts node_id or graph name.

**Parameters:**

- `bp_path` (string, required): AnimBlueprint asset path
- `state_machine` (string, required): State machine node GUID/N-id or state machine graph name

### `bp_delete_node`

Delete a node from a Blueprint graph Returns node ids that become stale after any graph mutation — re-run bp_describe_graph before reusing them.

**Parameters:**

- `bp_path` (string, required): Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints
- `graph_name` (string, required): Target graph name
- `node_id` (string, required): Node GUID

### `bp_add_variable`

Add a Blueprint member variable

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `var_name` (string, required): Variable name
- `var_type` (string, required): Variable type name
- `default_value` (string): Optional default value
- `category` (string): Optional category name

### `bp_remove_variable`

Remove a Blueprint member variable by name

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `var_name` (string, required): Variable name to remove

### `bp_rename_variable`

Rename a Blueprint member variable and fix up all graph references (via FBlueprintEditorUtils::RenameMemberVariable). Recompiles the Blueprint.

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `var_name` (string, required): Current variable name
- `new_name` (string, required): New variable name

### `bp_set_variable_flags`

Set editor-facing flags/metadata on an existing Blueprint member variable: instance_editable (shows in the Details panel of instances), blueprint_read_only, expose_on_spawn (requires instance_editable), category, tooltip. Only the provided fields are changed.

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `var_name` (string, required): Variable name
- `instance_editable` (bool): Editable per-instance (not Blueprint-only)
- `blueprint_read_only` (bool): Read-only in Blueprint graphs
- `expose_on_spawn` (bool): Expose as a SpawnActor pin (needs instance_editable)
- `category` (string): Variable category
- `tooltip` (string): Variable tooltip

### `bp_add_event_dispatcher`

Create an Event Dispatcher (multicast delegate) on a Blueprint — the core event-communication primitive. Other graphs Bind/Assign to it and Call it to broadcast. Optionally give it a typed signature via params (array of {name, type}, e.g. [{"name":"NewHealth","type":"float"}]). Recompiles the Blueprint.

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `dispatcher_name` (string, required): Event dispatcher name (convention: On*)
- `params` (array): Optional signature params: array of {name, type} (type like float/int/bool/string/vector/object:<Class>)

### `bp_remove_event_dispatcher`

Remove an Event Dispatcher (and its signature graph) from a Blueprint by name. Recompiles the Blueprint.

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `dispatcher_name` (string, required): Event dispatcher name to remove

### `bp_add_function_parameter`

Add an input or output parameter to an EXISTING Blueprint function (modifies its signature). direction=input adds to the function entry; direction=output adds to the return node (created if absent). Rejects a duplicate pin name. Recompiles.

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `function_name` (string, required): Existing function graph name
- `param_name` (string, required): Parameter name
- `param_type` (string, required): Parameter type (float/int/bool/string/vector/object:<Class>...)
- `direction` (string): input | output (default input)

### `bp_remove_function_parameter`

Remove a parameter (input or output) from an existing Blueprint function by name. Recompiles.

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `function_name` (string, required): Function graph name
- `param_name` (string, required): Parameter name to remove

### `bp_add_local_variable`

Add a local variable to a Blueprint function graph (scoped to that function). Recompiles.

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `function_name` (string, required): Function graph name
- `var_name` (string, required): Local variable name
- `var_type` (string, required): Variable type (float/int/bool/string/vector/object:<Class>...)
- `default_value` (string): Optional default value

### `bp_add_timeline`

Add a Timeline to a Blueprint (creates the timeline template/component). Add float/vector/color/event tracks and drive it from the event graph with a Timeline node (bp_create_node K2Node_Timeline). Recompiles.

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `timeline_name` (string, required): Timeline name

### `bp_add_component`

Add a component to a Blueprint SCS

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `component_class` (string, required): Component class name
- `component` (string, required): Component instance name
- `static_mesh` (string): Optional StaticMesh asset path for StaticMeshComponent
- `parent` (string): Optional parent component name to attach to

### `bp_remove_component`

Remove a component from a Blueprint SCS

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `component` (string, required): Component instance name to remove

### `bp_rename_component`

Rename a Blueprint SCS component variable (updates all graph references)

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `component` (string, required): Current component name
- `new_name` (string, required): New component name

### `bp_set_component_property`

Set a property on a Blueprint SCS or inherited component template

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `component` (string, required): Component name (SCS or inherited)
- `property_name` (string, required): Property name, or 'PostProcessMaterial' to add a blendable material
- `value` (string, required): Property value (string/number/bool), or material asset path for PostProcessMaterial

### `bp_describe_components`

Read back the full component tree and spec-governed properties (collision/Mobility/materials) of one Blueprint (bp_path) or every Blueprint under a folder (folder_path). Inherited-component gaps are explicitly flagged as inherited_unverifiable.

**Parameters:**

- `bp_path` (string): Single Blueprint asset path (provide this OR folder_path).
- `folder_path` (string): Content folder path for batch mode (provide this OR bp_path).
- `recursive` (boolean): Recurse into sub-folders. Default false.

### `bp_bulk_set_component_property`

Bulk-set generic component template properties on own SCS components in one Blueprint (bp_path) or every Blueprint directly under a folder (folder_path, non-recursive). Supports dotted/indexed property_path (e.g. RelativeLocation.Z, BodyInstance.bSimulatePhysics, OverrideMaterials[0]) plus semantic setters for collision, StaticMesh, Material[i], PostProcessMaterial, and ChildActorClass. Set include_inherited=true to edit parent-Blueprint SCS inherited components as child ICH override templates.

**Parameters:**

- `bp_path` (string): Single Blueprint asset path. Provide this OR folder_path.
- `folder_path` (string): Content folder (e.g. /Game/Vehicles); applies to all Blueprints directly under it (non-recursive). Leading /All is stripped. Provide this OR bp_path.
- `component_class` (string): Optional component class filter by class/superclass name, e.g. StaticMeshComponent. Empty = all component classes.
- `component` (string): Optional component variable/template name. Empty = all matching own SCS components.
- `edits` (array, required): Required array of objects {property_path,value}. property_path supports dotted/indexed paths and semantic keys Collision.ObjectType, Collision.Response.<Channel>, Collision.Profile, StaticMesh, Material[i]/Materials[i], PostProcessMaterial, ChildActorClass.
- `include_inherited` (boolean): Also target parent-Blueprint SCS inherited components by creating/reusing child InheritableComponentHandler override templates. Default false.
- `dry_run` (boolean): Preview changes without modifying or compiling. Default false.
- `defer_compile` (boolean): Queue changed Blueprints and flush compilation once at end. Default false.

### `bp_set_component_collision`

Bulk-set collision (object type + per-channel responses) on StaticMeshComponent templates inside one Blueprint (bp_path) or every Blueprint directly under a folder (folder_path, non-recursive). Switches the component to a Custom profile, then applies object type and responses via proper engine setters. Skips components whose StaticMesh has no collision geometry unless disabled.

**Parameters:**

- `bp_path` (string): Single Blueprint asset path. Provide this OR folder_path.
- `folder_path` (string): Content folder (e.g. /Game/MyVehicles); applies to all Blueprints directly under it (non-recursive). Provide this OR bp_path.
- `component` (string): Optional: only this StaticMeshComponent name. Empty = all StaticMeshComponents.
- `object_type` (string): Collision object type display name. Default 'Vehicle'.
- `responses` (object): Map of channel display name -> response, e.g. {"Pawn":"Ignore"}. Response is Ignore/Overlap/Block.
- `skip_if_no_mesh_collision` (boolean): Skip a component if its StaticMesh asset has no collision geometry. Default true.
- `dry_run` (boolean): Preview changes without modifying. Default false.

### `bp_override_function`

Override a parent class function in a Blueprint (creates proper override graph with correct signature)

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `function_name` (string, required): Parent function name to override

### `bp_compile`

Compile a Blueprint

**Parameters:**

- `bp_path` (string, required): Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints

### `bp_reparent`

Change the parent class of a Blueprint

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `new_parent_class` (string, required): New parent class name or Blueprint path

### `bp_copy_graph`

Copy a function graph from one Blueprint to another

**Parameters:**

- `source_bp` (string, required): Source Blueprint asset path
- `target_bp` (string, required): Target Blueprint asset path
- `graph_name` (string, required): Function graph name to copy
- `new_graph_name` (string): Optional new name for the copied graph
- `overwrite` (boolean): If true, removes existing graph with same name before copying

### `bp_remove_graph`

Remove a function graph or ubergraph page from a Blueprint

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `graph_name` (string, required): Graph name to remove

### `bp_rename_graph`

Rename a function graph or event graph page in a Blueprint (updates all internal call references)

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `graph_name` (string, required): Current graph name
- `new_name` (string, required): New graph name

### `bp_fixup_self_references`

Fix variable/function/component nodes to reference Self instead of a foreign parent class (use after bp_copy_graph across different class hierarchies)

**Parameters:**

- `bp_path` (string, required): Blueprint asset path

### `bp_fix_local_var_scope`

Fix stale local variable scope references in all function graphs (use after bp_rename_graph or bp_copy_graph when local variables show scope mismatch warnings)

**Parameters:**

- `bp_path` (string, required): Blueprint asset path

### `bp_get_summary`

Get Blueprint metadata summary: parent class, compile status, interfaces, variables (type, category, CDO default value, editable/read_only/replicated/transient/save_game/config/expose_on_spawn flags), functions (signature + local variables), macros, event graphs, custom events, input bindings, event dispatchers, timelines (tracks + curve assets + inlined keyframes), collapsed sub_graphs and components.

**Parameters:**

- `bp_path` (string, required): Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints
- `include_curve_keys` (boolean): Inline Timeline curve keyframes (t/v/interp). Default true. Blueprint-embedded curves are not reachable by read_curve, so this is the only way to see them.

### `bp_get_component_details`

Read component template properties (mobility, transform, absolute flags, physics, collision, visibility, mesh, materials) for a Blueprint. Covers own SCS + inherited components. Closes the gap where bp_get_summary only shows hierarchy. props filter accepts only the fixed supported groups.

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `component` (string): Optional: only this component name. Empty = all.
- `props` (string): Optional comma filter: transform,mobility,physics,rendering,mesh,collision. Default all.
- `include_inherited` (boolean): Include inherited (parent/native) components. Default true.

### `bp_get_class_members`

Get a Blueprint or native class's members grouped by owning class, with inheritance-chain attribution and token-conscious output controls. kinds, scope (self|chain|owner:<ClassName>) and detail (compact|full) are whitelisted enums.

**Parameters:**

- `bp_path` (string, required): Blueprint asset path OR native C++ class name (e.g. ACarPawn)
- `kinds` (string): Comma list: functions,variables,macros,delegates,interfaces. Default all.
- `scope` (string): self (default, only members declared in this class) | chain (full inheritance chain grouped by owner) | owner:<ClassName>
- `detail` (string): compact (default, names only) | full (signatures, types, flags)
- `limit` (integer): Max total members returned. Default 200.

### `bp_health_check`

Aggregate Blueprint health diagnostics: compile messages, unconnected required pins, broken references, and orphan impure nodes.

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `checks` (string): Optional comma filter: compile,unconnected_pins,broken_refs,orphan_nodes. Default all.
- `limit` (integer): Max items/messages per check. Default 50.

### `bp_diff`

Structural comparison of two Blueprints across parent, components, variables, functions, interfaces, and overrides.

**Parameters:**

- `bp_path_a` (string, required): First Blueprint asset path
- `bp_path_b` (string, required): Second Blueprint asset path
- `aspects` (string): Optional comma filter: parent,components,variables,functions,interfaces,overrides. Default all.

### `bp_trace_value`

Trace data-flow upstream or downstream from a node data pin in a Blueprint graph.

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `graph_name` (string, required): Graph name
- `node` (string, required): NodeGuid string or node title substring
- `pin` (string): Optional input pin name. Empty = all input data pins.
- `direction` (string): upstream (default) or downstream.
- `max_depth` (integer): Max recursive trace depth. Default 5.

### `bp_describe_graph`

Describe nodes in a Blueprint graph. mode: full(default)/compact/summary/node_pins/exec_chain. exec_chain mode follows exec pins from entry points (add entry_node param to start from specific N-id). Each node also carries a 'refs' object with the node-class-specific references it holds (FunctionReference, VariableReference, component template assets, bound graph names, cast target class, ...), plus state fields when set: 'enabled' (disabled/development_only), 'comment', 'error', and 'size' for comment boxes. Pins are annotated with 'parent'/'split_into' (split struct pins -- the parent's default is stale, read the sub-pins), 'orphaned', 'hidden', 'advanced' and 'label'.

**Parameters:**

- `bp_path` (string, required): Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints
- `graph_name` (string, required): Graph name
- `entry_node` (string): For exec_chain mode: N-id to start BFS from (default: all entry points)
- `include_refs` (boolean): Include the per-node 'refs' object of referenced functions/variables/properties. Default true (false in summary mode).

### `bp_compile_code`

Compile the limited Blueprint FUNCTION-graph DSL into an existing function graph. Builds function graphs ONLY -- no events (Tick/BeginPlay/Overlap/input), no nested if, no bare math. For event logic use atomic nodes (bp_override_function -> bp_create_node -> bp_batch_op -> bp_compile). Returns data.success (false on compile errors even when the request itself succeeds).

**Parameters:**

- `bp_path` (string, required): Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints
- `code` (string, required): Blueprint DSL text

### `bp_batch_op`

Execute multiple Blueprint atomic operations in a single transaction. Supports op aliases (connect/link/disconnect/unlink/set_default/set_value/create/add_node/delete/remove_node). Max 50 ops. Partial commit: failures do not stop subsequent ops. Node/pin ops require bp_path + graph_name and each operation must be a {op, params} object. Some ops return node ids that go stale after graph mutations -- re-run bp_describe_graph to refresh.

**Parameters:**

- `operations` (array, required): Array of operation objects {op, params}. Max 50.
- `bp_path` (string): Shared Blueprint asset path injected into each op (op-level overrides)
- `graph_name` (string): Shared graph name injected into each op (op-level overrides)

### `bp_validate_code`

Validate the limited Blueprint FUNCTION-graph DSL syntax (read-only, no mutation, no compile). Same narrow grammar as bp_compile_code; not for events/graph editing.

**Parameters:**

- `code` (string, required): Blueprint DSL text

### `bp_search`

Search nodes in a Blueprint by name (substring, case-insensitive) and/or type (exact class name). Searches all graphs (event, function, macro). Matched nodes include a 'refs' object with their node-class-specific references (called function, read variable/property, cast target, ...).

**Parameters:**

- `bp_path` (string, required): Blueprint asset path, or 'level:current' / 'level:/Game/Maps/MyMap' for Level Blueprints
- `name` (string): Substring to match against node title (case-insensitive). Empty = no filter.
- `type` (string): Exact node class name to match (e.g. 'K2Node_CallFunction'). Empty = no filter.
- `verbose` (boolean): If true, include pins (in/out) for each matched node. Default false.
- `limit` (integer): Maximum number of nodes to return. Default 100.

## Curve

### `create_curve`

Create a curve asset (Float, LinearColor, or Vector) with optional keyframes

**Parameters:**

- `name` (string, required): Curve asset name
- `path` (string, required): Content folder path (e.g. /Game/Curves)
- `curve_type` (string): Float | LinearColor | Vector (default Float)
- `keys` (array): Optional keys. Float:[{time,value}] Vector:[{time,x,y,z}] LinearColor:[{time,r,g,b,a}]

### `read_curve`

Read a curve's type and keyframes. Accepts a curve asset path, or a Blueprint-embedded Timeline curve sub-object path (e.g. /Game/Foo/BP_Bar.BP_Bar_C:CurveFloat_0, as reported by bp_get_summary timelines[].tracks[].curve).

**Parameters:**

- `curve_path` (string, required): Curve asset path, or Package.Object:SubObject path for a Blueprint-embedded Timeline curve

### `create_curve_atlas`

Create a CurveLinearColorAtlas, optionally seeded with CurveLinearColor assets

**Parameters:**

- `name` (string, required): Atlas asset name
- `path` (string, required): Content folder path
- `width` (int): Texture width (default 256)
- `height` (int): Texture height (default 256)
- `curves` (array): Optional array of CurveLinearColor asset paths to add as gradients

### `read_curve_atlas`

Read a CurveLinearColorAtlas: texture size and gradient curve paths

**Parameters:**

- `atlas_path` (string, required): Atlas asset path

## Data

### `create_data_asset`

Create a Data Asset from a CONCRETE, non-abstract UDataAsset subclass (abstract bases like UDataAsset/UPrimaryDataAsset are rejected).

**Parameters:**

- `name` (string, required): Data asset name
- `path` (string, required): Content folder path
- `class_path` (string, required): Concrete UDataAsset subclass, required (e.g. /Game/BP_MyData.BP_MyData_C or /Script/MyGame.MyData). UDataAsset/UPrimaryDataAsset bases are abstract.

### `read_data_asset`

Read a Data Asset's class and all UPROPERTY values via reflection

**Parameters:**

- `asset_path` (string, required): Data asset path

### `data_create_table`

Create a DataTable asset using a row struct

**Parameters:**

- `name` (string, required): DataTable asset name
- `path` (string, required): Content folder path (e.g. /Game/Data)
- `row_struct` (string, required): Row UScriptStruct path or name

### `data_add_row`

Add or replace a row in a DataTable from JSON object data

**Parameters:**

- `table_path` (string, required): DataTable asset path
- `row_name` (string, required): Row name to add or replace
- `row_data` (object, required): JSON object containing row field values

### `data_read_table`

Read all rows or a single row from a DataTable

**Parameters:**

- `table_path` (string, required): DataTable asset path
- `row_name` (string): Optional row name to read

### `data_import_json`

Import DataTable rows from a JSON string

**Parameters:**

- `table_path` (string, required): DataTable asset path
- `json_string` (string, required): JSON string accepted by UDataTable::CreateTableFromJSONString

### `data_create_struct`

Create a UserDefinedStruct asset with typed fields

**Parameters:**

- `name` (string, required): Struct asset name
- `path` (string, required): Content folder path (e.g. /Game/Data)
- `fields` (array, required): Array of field objects with name and type

### `data_create_enum`

Create a UserDefinedEnum asset with entries

**Parameters:**

- `name` (string, required): Enum asset name
- `path` (string, required): Content folder path (e.g. /Game/Data)
- `entries` (array, required): Array of enum entry names

## Debug

### `bp_set_breakpoint`

Set/enable a breakpoint on a Blueprint node by NodeGuid.

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `graph_name` (string): Graph name (EventGraph / function name)
- `node_id` (string): Node GUID
- `focus` (boolean): Open the Blueprint editor and jump to the node after the operation (default true)

### `bp_clear_breakpoint`

Remove a breakpoint from a Blueprint node by NodeGuid.

**Parameters:**

- `bp_path` (string, required): Blueprint asset path
- `graph_name` (string): Graph name (EventGraph / function name)
- `node_id` (string): Node GUID
- `focus` (boolean): Open the Blueprint editor and jump to the node after the operation (default true)

### `bp_list_breakpoints`

List all breakpoints in a Blueprint with their graph, node GUID, title, and enabled state.

**Parameters:**

- `bp_path` (string, required): Blueprint asset path

## Dialog

### `get_active_dialog`

Report whether a blocking modal editor dialog is currently open (title/type), the armed auto-response mode, and how many dialogs SmithUE has auto-dismissed. WORKER-SAFE: this still responds while a modal dialog has jammed the game thread. Read-only, no mutation.

### `dismiss_active_dialog`

Close a modal editor dialog that is blocking the game thread (e.g. an unexpected 'Save As'/confirm prompt). WORKER-SAFE. success = QUEUED, not finished: the close is applied on the next modal-loop tick; poll get_active_dialog (modal_active=false) to confirm. button_text clicks a SPECIFIC button by its label (case-insensitive; exact then substring; check get_active_dialog 'buttons') — use this for dialogs whose default button is NOT the one you want (e.g. OkCancel confirms that default to Cancel). response=cancel (default) reliably destroys/closes the window; response=accept is BEST-EFFORT (focus + Enter = default action) and falls back to close if Enter does not dismiss it.

**Parameters:**

- `response` (string): How to respond: 'cancel' (default, reliably closes) or 'accept' (best-effort default action). Ignored when button_text is provided.
- `button_text` (string): Click the button whose label matches this text (case-insensitive; exact match preferred, then substring). See get_active_dialog 'buttons' for available labels.

### `set_dialog_auto_response`

Arm a persistent auto-responder so ANY modal dialog that opens is answered automatically (prevents automation from hanging on unexpected prompts). WORKER-SAFE — arm this BEFORE running tools that might pop a modal (e.g. level_save on an unsaved level). mode=off (default, disarmed) | cancel (reliably close every modal) | accept (best-effort default action, falls back to close).

**Parameters:**

- `mode` (string, required): Auto-response mode: 'off' (disarm), 'cancel' (auto-close every modal), or 'accept' (best-effort default action)

## Editor

### `spawn_actor`

Spawn an actor in the current level

**Parameters:**

- `class` (string, required): Actor class name (e.g. StaticMeshActor, PointLight)
- `name` (string): Actor label shown in World Outliner
- `location` (object): Spawn location {x,y,z}
- `rotation` (object): Spawn rotation {pitch,yaw,roll}
- `scale` (object): Scale {x,y,z}

### `spawn_mesh_actor`

Spawn a StaticMeshActor into the current level with a mesh and optional material. Safe (no world switch).

**Parameters:**

- `mesh` (string, required): Static mesh asset path, e.g. /Engine/BasicShapes/Cube.Cube
- `material` (string): Optional material asset path applied to slot 0
- `location` (object): Spawn location {x,y,z}
- `rotation` (object): Spawn rotation {pitch,yaw,roll}
- `scale` (object): Scale {x,y,z}
- `label` (string): Actor label in World Outliner

### `get_all_actors`

List all actors in the current level

**Parameters:**

- `class_filter` (string): Optional class name filter

### `set_actor_property`

Set a reflected property on an actor by label

**Parameters:**

- `actor_label` (string, required): Actor label
- `property_name` (string, required): Property name
- `value` (string, required): Property value (string/number/bool)

### `delete_actor`

Delete an actor from the current level by label

**Parameters:**

- `actor_label` (string, required): Actor label

### `add_postprocess_material`

Add a material to a PostProcessVolume's blendable list

**Parameters:**

- `actor_label` (string, required): PostProcessVolume actor label
- `material_path` (string, required): Material asset path (e.g. /Game/Materials/M_PP_ThermalVision)
- `weight` (number): Blend weight (0.0-1.0, default 1.0)

### `open_map`

Open a map asset in the Unreal Editor

**Parameters:**

- `map_path` (string, required): Map asset path (e.g. /Game/Maps/MyMap)

### `get_project_setting`

Read a project configuration setting from INI file

**Parameters:**

- `section` (string, required): INI section name
- `key` (string, required): INI key name
- `config_file` (string): Config file: Engine (default), Editor, Game, Input

### `set_project_setting`

Write a project configuration setting to INI file and flush to disk

**Parameters:**

- `section` (string, required): INI section name
- `key` (string, required): INI key name
- `value` (string, required): Value to set
- `config_file` (string): Config file: Engine (default), Editor, Game, Input

### `auto_layout_graph`

Auto-arrange nodes in any graph (Material, Blueprint, Niagara). Closes editor if open to prevent save conflicts.

**Parameters:**

- `asset_path` (string, required): Full asset path (e.g. /Game/Materials/M_Test)
- `graph_name` (string): Graph name filter (e.g. 'EventGraph'). For materials, omit.
- `direction` (string): Layout direction: 'left_to_right' (default) or 'top_to_bottom'
- `spacing_x` (number): Horizontal spacing between layers (default: 400)
- `spacing_y` (number): Vertical spacing between nodes in same layer (default: 200)

## Environment

### `env_set_post_process`

Configure post-process volume settings

**Parameters:**

- `actor_label` (string): PostProcessVolume actor label (first found if omitted)
- `settings` (object, required): Settings object: bloom_intensity, exposure_compensation, color_saturation, etc.

### `env_set_fog`

Set exponential height fog properties

**Parameters:**

- `actor_label` (string): Fog actor label (first found if omitted)
- `settings` (object, required): Settings: density, height_falloff, start_distance, color{r,g,b}

### `env_set_sky_atmosphere`

Set sky atmosphere parameters

**Parameters:**

- `settings` (object, required): Settings: rayleigh_scattering_scale, mie_scattering_scale, atmosphere_height

### `env_set_light`

Set directional/point/spot light properties

**Parameters:**

- `actor_label` (string, required): Light actor label
- `settings` (object, required): Settings: intensity, color{r,g,b}, temperature, use_temperature, attenuation_radius

### `env_set_physics`

Enable/disable physics simulation on an actor

**Parameters:**

- `actor_label` (string, required): Actor label
- `simulate` (bool, required): Enable or disable physics
- `gravity_override` (float): Optional gravity scale override

### `env_set_collision`

Set collision profile/preset on an actor

**Parameters:**

- `actor_label` (string, required): Actor label
- `profile_name` (string, required): Collision profile name (e.g. BlockAll, OverlapAll, NoCollision)

### `env_get_physics_info`

Get physics/collision info for an actor

**Parameters:**

- `actor_label` (string, required): Actor label

### `env_create_spline`

Create a spline actor with specified points

**Parameters:**

- `name` (string, required): Actor label name
- `points` (array, required): Array of {x,y,z} point objects
- `closed` (bool): Whether the spline is closed loop

### `env_add_spline_point`

Add a point to an existing spline actor

**Parameters:**

- `actor_label` (string, required): Spline actor label
- `position` (object, required): {x,y,z} position
- `index` (int): Insert index (appends if omitted)

### `env_set_spline_point`

Modify a spline point position and tangent

**Parameters:**

- `actor_label` (string, required): Spline actor label
- `index` (int, required): Point index to modify
- `position` (object, required): {x,y,z} new position
- `tangent` (object): Optional {x,y,z} arrive tangent

### `env_get_spline_info`

Get spline point count, length, closed state

**Parameters:**

- `actor_label` (string, required): Spline actor label

## Input

### `input_create_action`

Create an Enhanced Input Action asset (IA_*)

**Parameters:**

- `name` (string, required): Action name (IA_ prefix added if missing)
- `path` (string): Content folder path (default: /Game/Input)
- `value_type` (string): Value type: Boolean, Axis1D, Axis2D, Axis3D (default: Boolean)

### `input_create_mapping_context`

Create an Input Mapping Context asset (IMC_*) with optional key mappings

**Parameters:**

- `name` (string, required): Context name (IMC_ prefix added if missing)
- `path` (string): Content folder path (default: /Game/Input)
- `mappings` (array): Array of {action, key, modifiers[]} objects

### `input_find_actions`

Find InputAction and InputMappingContext assets in the project

**Parameters:**

- `search_term` (string): Optional filter by name substring

### `input_read_mapping_context`

Read an InputMappingContext to see its action-key mappings and modifiers

**Parameters:**

- `name` (string, required): IMC asset name or path

### `input_edit_mapping_context`

Edit an InputMappingContext: add or remove key mappings

**Parameters:**

- `name` (string, required): IMC asset name or full path
- `add_mappings` (array): Array of {action, key, modifiers[]} to add
- `remove_actions` (array): Array of action names to unmap entirely

### `input_delete_asset`

Delete an InputAction or InputMappingContext asset by name or path

**Parameters:**

- `name` (string): Asset name to search and delete
- `path` (string): Full asset path to delete directly

## Insights

### `insights_start_trace`

Start capturing an Unreal Insights trace to a .utrace file (FTraceAuxiliary). Enable channels to control what is recorded; default 'default,stat,counter' covers frames + CPU + stats + counters (good for perf-triage). If path is omitted the file goes to the project's Saved/Profiling with a timestamped name. Only one trace can be active — call insights_stop_trace first. Analyze the result with analyze_trace.

**Parameters:**

- `path` (string): Output .utrace path (default: Saved/Profiling/SmithUE_<timestamp>.utrace)
- `channels` (string): Comma-separated trace channels (default 'default,stat,counter'; e.g. add 'gpu','loadtime','memory')

### `insights_stop_trace`

Stop the active Unreal Insights trace and flush the .utrace file. Returns the file path + size. Then run analyze_trace on it.

### `insights_trace_status`

Report whether an Insights trace is currently active/paused and the last destination path started by this plugin. Read-only.

### `analyze_trace`

Analyze a .utrace file offline (TraceServices) into a perf summary: session duration, and for the Game and Rendering threads the frame-time distribution (count, avg/min/max/median/p95/p99 ms, avg FPS, hitch_count over hitch_ms). Optionally summarize stat counters (min/max/avg/last). This is the primary perf-triage readout; top CPU timers are not yet included.

**Parameters:**

- `path` (string, required): .utrace file path to analyze
- `hitch_ms` (number): Frame time (ms) above which a frame counts as a hitch (default 33.3)
- `counters` (bool): Also summarize stat counters (default false)
- `max_counters` (int): Max counters to report when counters=true (default 40)

## Interaction

### `execute_editor_command`

Execute a named Unreal Editor command by name via GEditor->Exec

**Parameters:**

- `command_name` (string, required): Editor command name to execute (e.g. ACTOR SELECT ALL)

### `execute_console_command`

Execute a UE console command in the current editor world

**Parameters:**

- `command` (string, required): Console command string to execute (e.g. r.ScreenPercentage 100)

### `list_editor_commands`

List all registered FUICommandInfo entries across all input binding contexts

**Parameters:**

- `filter` (string): Optional case-insensitive substring filter applied to command name or label

### `undo`

Undo the last editor transaction

### `redo`

Redo the last undone editor transaction

### `simulate_key`

Simulate a key press (command-lookup first, Slate fallback)

**Parameters:**

- `key` (string, required): Key name: A-Z, F1-F12, Delete, Enter, Escape, Tab, Space, LeftArrow, RightArrow, UpArrow, DownArrow, Home, End, PageUp, PageDown, Num0-Num9
- `modifiers` (object): Optional modifiers: {ctrl,shift,alt,cmd} booleans

### `list_key_bindings`

List all registered key bindings (commands with active key chords)

**Parameters:**

- `filter` (string): Optional substring filter on command name or label
- `context` (string): Optional binding context filter (e.g. LevelEditor)

## Level

### `level_new`

Create a new blank level/map (deferred next-frame creation, crash-safe). Query level state after ~1s. Executes on the next frame — success means the operation was QUEUED, not finished; query level state after ~1s.

**Parameters:**

- `name` (string, required): New level name
- `path` (string): Optional package path or filename for saving the new map

### `level_open`

Open an existing level/map. Executes on the next frame — success means the operation was QUEUED, not finished; query level state after ~1s.

**Parameters:**

- `level_path` (string, required): Map package path or filename to open

### `level_save`

Save the current level. Pass level_path to Save-As to an explicit /Game package path WITHOUT any dialog (recommended for unsaved/untitled levels). WITHOUT level_path an unsaved level opens a BLOCKING 'Save As' modal on the game thread (use the Dialog domain tools to detect/close it, or just pass level_path).

**Parameters:**

- `level_path` (string): Optional target /Game package path for Save-As (e.g. '/Game/Maps/MyLevel'). A trailing '/' appends the current map name. Omit to save in place.

### `level_get_info`

Get current level name, path, and actor count

### `level_create_landscape`

Create a landscape in the current level

**Parameters:**

- `section_size` (integer): Landscape section size
- `sections_x` (integer): Number of landscape sections along X
- `sections_y` (integer): Number of landscape sections along Y
- `material_path` (string): Optional landscape material path

### `level_set_landscape_material`

Set the material on all landscape proxies in the current level

**Parameters:**

- `material_path` (string, required): Material asset path

### `level_get_landscape_info`

Get landscape proxy component counts, dimensions, and materials

### `level_add_foliage_type`

Add a static mesh foliage type to the current level

**Parameters:**

- `static_mesh_path` (string, required): Static mesh asset path

### `level_paint_foliage`

Add foliage instances at explicit locations

**Parameters:**

- `mesh_path` (string, required): Static mesh asset path
- `locations` (array, required): Array of {x,y,z} objects or [x,y,z] arrays
- `density` (float): Optional foliage type density

### `level_erase_foliage`

Remove foliage instances within a radius

**Parameters:**

- `mesh_path` (string): Optional static mesh asset path filter
- `center` (object, required): Center point as {x,y,z}
- `radius` (float, required): Erase radius

### `level_get_foliage_stats`

Count foliage types and instances in the current level

### `level_add_basic_env`

Add basic environment to the CURRENT level: directional light, sky atmosphere, sky light, height fog, player start, and a floor. Safe (no world switch).

**Parameters:**

- `floor_scale` (number): Uniform floor plane scale (default 20)

## LiveCoding

### `livecoding_status`

Report Unreal Live Coding support and state (read-only). Safe to call during PIE.

### `livecoding_compile`

Trigger an Unreal Live Coding C++ hot-compile and report the result. Blocks until the compile completes. NOTE: the C++ compiler error text is NOT returned by Unreal - on failure use show_console or read the editor log. New UCLASS/UPROPERTY/UFUNCTION/header changes cannot hot-patch and need a full editor rebuild.

**Parameters:**

- `show_console` (bool): Explicitly pre-open the Live Coding console window (default false). Note: UE may show the console on its own during compile regardless, so false does not guarantee it stays hidden.

## Material

### `create_material`

Create a new UMaterial asset

**Parameters:**

- `name` (string, required): Asset name
- `path` (string, required): Content folder path, e.g. /Game/Materials

### `get_material_info`

Get information about a material including its expressions

**Parameters:**

- `material_path` (string, required): Full asset path, e.g. /Game/Materials/M_Test

### `add_material_expression`

Add a material expression node. If position_x/position_y are omitted the node is auto-cascaded (no overlap). After wiring the graph, call auto_layout_graph on the material for a clean left-to-right layout (source nodes left, result-feeding node rightmost).

**Parameters:**

- `material_path` (string, required): Full asset path
- `expression_class` (string, required): Expression class name, e.g. MaterialExpressionConstant3Vector
- `position_x` (number): Editor X position
- `position_y` (number): Editor Y position

### `connect_material_pins`

Connect material expression pins. Use dest_expression_index=-1 to connect to the material output (dest_input_index picks BaseColor/Metallic/... — see its description). Tip: after all connections, run auto_layout_graph on the material to tidy the wiring.

**Parameters:**

- `material_path` (string, required): Full asset path
- `source_expression_index` (number, required): Index of source expression in Expressions array
- `source_output_index` (number): Output pin index on source expression
- `dest_expression_index` (number, required): Index of dest expression, or -1 for material output
- `dest_input_index` (number): Input pin index. For material output (dest_expression_index=-1): 0=BaseColor,1=Metallic,2=Roughness,3=Normal,4=EmissiveColor,5=Opacity,6=OpacityMask,7=WorldPositionOffset (WPO), 8=FrontMaterial (UE5.8 Substrate root; only valid when the project has r.Substrate=1). 9+ unsupported.

### `disconnect_material_pins`

Break a connection at a destination input pin. dest_expression_index=-1 targets the material output (dest_input_index picks BaseColor/Metallic/... as in connect_material_pins); otherwise it targets that expression's input pin. Clears whatever was feeding that pin.

**Parameters:**

- `material_path` (string, required): Full asset path
- `dest_expression_index` (number, required): Index of dest expression, or -1 for material output
- `dest_input_index` (number): Input pin index (same mapping as connect_material_pins)

### `remove_material_expression`

Remove an expression node from a material by its index in the Expressions array (from get_material_info). Also clears any input pins (on other expressions and the material output) that were fed by it, so no dangling wires remain. Expression indices shift after removal. MUTATES; run compile_material afterwards.

**Parameters:**

- `material_path` (string, required): Full asset path
- `expression_index` (number, required): Index of expression in Expressions array

### `compile_material`

Trigger material recompilation

**Parameters:**

- `material_path` (string, required): Full asset path

### `set_material_property`

Set material properties (domain, blend_mode, shading_model, two_sided, blendable_location)

**Parameters:**

- `material_path` (string, required): Full asset path
- `domain` (string): Material domain: surface, deferred_decal, light_function, volume, post_process, ui
- `blend_mode` (string): Blend mode: opaque, masked, translucent, additive, modulate, alpha_composite, alpha_holdout
- `shading_model` (string): Shading model: unlit, default_lit, subsurface, clear_coat, etc.
- `two_sided` (bool): Enable two-sided rendering
- `blendable_location` (string): For PostProcess: before_tonemapping, after_tonemapping, before_translucency, replacing_tonemapper, ssr_input

### `set_expression_property`

Set properties on a material expression node (e.g. Custom HLSL code, constant values, texture)

**Parameters:**

- `material_path` (string, required): Full asset path
- `expression_index` (number, required): Index of expression in Expressions array
- `properties` (object, required): Key-value pairs; valid keys are NODE-TYPE-SPECIFIC. Constant: value (NOT 'R'). Constant3Vector: r,g,b. ScalarParameter: parameter_name,default_value. VectorParameter: parameter_name,r,g,b,a. CollectionParameter: collection,parameter_name. TextureSample: sampler_type,texture. Custom(HLSL): code,output_type(float/float2/float3/float4),inputs([{name}]). MaterialFunctionCall: material_function. SceneTexture nodes: scene_texture_id. ALL nodes: description. On a key/type mismatch the error lists this node's valid keys. (Transform-node space is NOT settable.)

### `create_mpc`

Create a new Material Parameter Collection asset

**Parameters:**

- `name` (string, required): Asset name
- `path` (string, required): Content folder path, e.g. /Game/Materials

### `add_mpc_scalar`

Add a scalar parameter to a Material Parameter Collection

**Parameters:**

- `mpc_path` (string, required): Full asset path to the MPC
- `param_name` (string, required): Parameter name
- `default_value` (number): Default scalar value (default: 0.0)

### `add_mpc_vector`

Add a vector parameter to a Material Parameter Collection

**Parameters:**

- `mpc_path` (string, required): Full asset path to the MPC
- `param_name` (string, required): Parameter name
- `default_r` (number): Default R value (default: 0.0)
- `default_g` (number): Default G value (default: 0.0)
- `default_b` (number): Default B value (default: 0.0)
- `default_a` (number): Default A value (default: 1.0)

### `set_mpc_value`

Update the default value of a scalar parameter in a Material Parameter Collection

**Parameters:**

- `mpc_path` (string, required): Full asset path to the MPC
- `param_name` (string, required): Parameter name
- `value` (string, required): New scalar value as float string

### `create_material_instance`

Create a new MaterialInstanceConstant asset from a parent material

**Parameters:**

- `name` (string, required): Asset name
- `path` (string, required): Content folder path, e.g. /Game/Materials
- `parent_material` (string, required): Full asset path of the parent UMaterial or UMaterialInstance

### `set_mi_scalar`

Set a scalar parameter override on a MaterialInstanceConstant

**Parameters:**

- `mi_path` (string, required): Full asset path to the MaterialInstanceConstant
- `param_name` (string, required): Scalar parameter name
- `value` (number, required): Scalar value to set

### `set_mi_vector`

Set a vector parameter override on a MaterialInstanceConstant

**Parameters:**

- `mi_path` (string, required): Full asset path to the MaterialInstanceConstant
- `param_name` (string, required): Vector parameter name
- `r` (number): Red channel (default: 0.0)
- `g` (number): Green channel (default: 0.0)
- `b` (number): Blue channel (default: 0.0)
- `a` (number): Alpha channel (default: 1.0)

### `get_mi_info`

Get info about a MaterialInstanceConstant: parent name and all scalar/vector parameter overrides

**Parameters:**

- `mi_path` (string, required): Full asset path to the MaterialInstanceConstant

### `create_material_function`

Create a new UMaterialFunction asset

**Parameters:**

- `name` (string, required): Asset name
- `path` (string, required): Content folder path, e.g. /Game/Materials
- `description` (string): Function description
- `expose_to_library` (bool): Expose to material function library (default: true)
- `library_categories` (string): Comma-separated library categories, e.g. 'PostProcess,Utility'

### `get_material_function_info`

Get information about a material function including its expressions

**Parameters:**

- `function_path` (string, required): Full asset path, e.g. /Game/Materials/MF_Test

### `add_mf_expression`

Add a material expression node to a material function

**Parameters:**

- `function_path` (string, required): Full asset path
- `expression_class` (string, required): Expression class name, e.g. MaterialExpressionFunctionInput
- `position_x` (number): Editor X position
- `position_y` (number): Editor Y position

### `connect_mf_pins`

Connect material expression pins within a material function

**Parameters:**

- `function_path` (string, required): Full asset path
- `source_expression_index` (number, required): Index of source expression in FunctionExpressions array
- `source_output_index` (number): Output pin index on source expression
- `dest_expression_index` (number, required): Index of dest expression
- `dest_input_index` (number): Input pin index on dest expression

### `disconnect_mf_pins`

Break a connection at a destination input pin inside a material function. Clears whatever expression was feeding dest_expression_index's input pin (material functions have no material output; connect a FunctionOutput node's input instead).

**Parameters:**

- `function_path` (string, required): Full asset path
- `dest_expression_index` (number, required): Index of dest expression in FunctionExpressions array
- `dest_input_index` (number): Input pin index on dest expression

### `remove_mf_expression`

Remove an expression node from a material function by its index in the FunctionExpressions array (from get_material_function_info). Also clears any input pins on other expressions that were fed by it, so no dangling wires remain. Expression indices shift after removal. MUTATES the asset.

**Parameters:**

- `function_path` (string, required): Full asset path
- `expression_index` (number, required): Index of expression in FunctionExpressions array

### `set_mf_expression_property`

Set properties on a material function expression node (Custom HLSL code, constant values, function input/output names, etc.)

**Parameters:**

- `function_path` (string, required): Full asset path
- `expression_index` (number, required): Index of expression in FunctionExpressions array
- `properties` (object, required): Key-value pairs. Custom: code, output_type, description, inputs. FunctionInput: input_name, input_type, preview_value. FunctionOutput: output_name

## Mesh

### `read_mesh_info`

Read a Static Mesh's build state: LOD count + screen sizes, material slot count, simple-collision primitive count, Nanite enabled flag, and LOD0 vertex count. The assertion companion for mesh build tools. Read-only.

**Parameters:**

- `mesh_path` (string, required): Static Mesh asset path

### `mesh_add_collision`

Add a simple collision primitive to a Static Mesh (auto-fits the mesh bounds). shape = box/sphere/capsule/ndop10x/ndop10y/ndop10z/ndop18/ndop26. Adds to any existing collision. Returns the new collision primitive count. MUTATES; call save_asset.

**Parameters:**

- `mesh_path` (string, required): Static Mesh asset path
- `shape` (string, required): box/sphere/capsule/ndop10x/ndop18/ndop26

### `mesh_remove_collision`

Remove ALL simple collision primitives from a Static Mesh. MUTATES; call save_asset.

**Parameters:**

- `mesh_path` (string, required): Static Mesh asset path

### `mesh_set_nanite`

Enable or disable Nanite on a Static Mesh (rebuilds the mesh). MUTATES; call save_asset.

**Parameters:**

- `mesh_path` (string, required): Static Mesh asset path
- `enabled` (bool, required): true to enable Nanite, false to disable

### `mesh_generate_lods`

Auto-generate reduction LODs for a Static Mesh. lod_count is the total number of LODs (incl. LOD0). Each successive LOD halves the triangle percentage (LOD0=100%, LOD1=50%, ...); screen sizes are auto-computed. Replaces existing LODs. MUTATES; call save_asset.

**Parameters:**

- `mesh_path` (string, required): Static Mesh asset path
- `lod_count` (int, required): Total LOD count incl. LOD0 (1-8)

### `mesh_set_material`

Assign a material to a Static Mesh or Skeletal Mesh material slot. slot = a 0-based slot index or a slot name (from read_mesh_info / read_skeletal_mesh_info). material_path is a Material or Material Instance asset. MUTATES; call save_asset.

**Parameters:**

- `mesh_path` (string, required): Static or Skeletal Mesh asset path
- `slot` (string, required): Material slot index or slot name
- `material_path` (string, required): Material/MaterialInstance asset path

### `read_skeletal_mesh_info`

Read a Skeletal Mesh's state: material slots (name + assigned material), skeleton, physics asset, LOD count, socket count. Read-only.

**Parameters:**

- `mesh_path` (string, required): Skeletal Mesh asset path

## Niagara

### `create_niagara_system`

Create a new UNiagaraSystem asset at the given content path

**Parameters:**

- `name` (string, required): Asset name, e.g. NS_Radar
- `path` (string, required): Content folder path, e.g. /Game/Radar/Niagara

### `niagara_get_system_info`

Get information about a Niagara system including emitters and user parameters

**Parameters:**

- `system_path` (string, required): Full asset path, e.g. /Game/Radar/Niagara/NS_Radar

### `niagara_add_emitter`

Add a new emitter to an existing Niagara system

**Parameters:**

- `system_path` (string, required): Full asset path to the Niagara system
- `emitter_name` (string, required): Name for the new emitter

### `niagara_add_emitter_from_template`

Add an emitter to a Niagara system from a specified template asset path

**Parameters:**

- `system_path` (string, required): Full asset path to the Niagara system
- `emitter_name` (string, required): Name for the new emitter
- `template_path` (string, required): Full asset path to the template emitter

### `niagara_set_emitter_property`

Set a property on a Niagara emitter. Supported properties: enabled (bool), local_space (bool)

**Parameters:**

- `system_path` (string, required): Full asset path to the Niagara system
- `emitter_name` (string, required): Name of the emitter to modify
- `property` (string, required): Property name: enabled, local_space
- `value` (string, required): Property value as string, e.g. 'true' or 'false'

### `niagara_compile`

Compile a Niagara system and save the asset

**Parameters:**

- `system_path` (string, required): Full asset path to the Niagara system

### `niagara_add_renderer`

Add a renderer (sprite, mesh, or ribbon) to an emitter

**Parameters:**

- `system_path` (string, required): Full asset path to the Niagara system
- `emitter_name` (string, required): Name of the emitter to add renderer to
- `renderer_type` (string, required): Renderer type: sprite, mesh, or ribbon

### `niagara_set_renderer_property`

Set a property on a Niagara renderer. Supports Material (asset path), and UObject properties via reflection

**Parameters:**

- `system_path` (string, required): Full asset path to the Niagara system
- `emitter_name` (string, required): Name of the emitter
- `property_name` (string, required): Property to set (e.g. Material, Alignment)
- `value` (string, required): Value to set (asset path for Material, or string representation)
- `renderer_index` (string): Renderer index (default: 0)

### `niagara_add_module`

Add a Niagara module script to an emitter's stack group

**Parameters:**

- `system_path` (string, required): Full asset path to the Niagara system
- `emitter_name` (string, required): Name of the emitter
- `module_path` (string, required): Asset path to the Niagara module script
- `stack_group` (string, required): Stack group: spawn or update

### `niagara_set_module_input`

Set an input value on a Niagara module

**Parameters:**

- `system_path` (string, required): Full asset path to the Niagara system
- `emitter_name` (string, required): Name of the emitter
- `module_name` (string, required): Module name/namespace
- `input_name` (string, required): Input parameter name
- `value` (string, required): Value to set
- `value_type` (string, required): Value type: float, int, bool, vector2, vector, color
- `stack_group` (string, required): Stack group: spawn, update, emitter_spawn, emitter_update

### `niagara_add_user_parameter`

Add a user parameter to a Niagara system for Blueprint interaction

**Parameters:**

- `system_path` (string, required): Full asset path to the Niagara system
- `param_name` (string, required): Parameter name (without 'User.' prefix)
- `param_type` (string, required): Type: float, int, bool, vector, color, position
- `default_value` (string): Optional default value

### `spawn_niagara_actor`

Spawn a NiagaraActor in the level with a given system asset, auto-activated

**Parameters:**

- `system_path` (string, required): Full asset path to the Niagara system
- `name` (string): Actor label
- `location` (object): Spawn location {x,y,z}

### `niagara_static_switch`

Get or set static switch values on a Niagara module. Omit switch_name to list all switches.

**Parameters:**

- `system_path` (string, required): Full asset path to the Niagara system
- `emitter_name` (string, required): Name of the emitter
- `module_name` (string, required): Module function name (e.g. InitializeParticle)
- `switch_name` (string): Static switch name to set (omit to list all)
- `value` (string): Value to set (enum index as string, e.g. '1')

### `niagara_search_assets`

Search Niagara assets (systems and emitters) in the project via AssetRegistry

**Parameters:**

- `search_path` (string): Content folder path to search under, default /Game
- `class_filter` (string): Asset class filter: all, system, emitter
- `name_pattern` (string): Optional substring to match asset names

### `niagara_delete_renderer`

Delete a renderer from an emitter by index

**Parameters:**

- `system_path` (string, required): Full asset path to the Niagara system
- `emitter_name` (string, required): Name of the emitter
- `renderer_index` (string): Renderer index to delete (default: 0)

### `niagara_delete_module`

Delete a module from an emitter's stack by function name

**Parameters:**

- `system_path` (string, required): Full asset path to the Niagara system
- `emitter_name` (string, required): Name of the emitter
- `module_name` (string, required): Module function name to delete
- `stack_group` (string, required): Stack group: spawn, update, emitter_spawn, emitter_update

### `niagara_delete_emitter`

Delete an emitter from a Niagara system by name

**Parameters:**

- `system_path` (string, required): Full asset path to the Niagara system
- `emitter_name` (string, required): Name of the emitter to delete

## Observation

### `list_panels`

Lists all known editor panels and whether each is currently open.

### `open_panel`

Opens (or focuses) a named editor panel tab.

**Parameters:**

- `panel_name` (string, required): Tab ID of the panel to open, e.g. ContentBrowserTab1

### `close_panel`

Closes a named editor panel tab if it is open.

**Parameters:**

- `panel_name` (string, required): Tab ID of the panel to close, e.g. OutputLog

### `get_editor_state`

Returns a snapshot of the current editor state: PIE, simulation, selection, level, viewport.

### `get_actor_property`

Read a reflected property value from an actor by label

**Parameters:**

- `actor_label` (string, required): Actor label
- `property_name` (string, required): Property name

### `get_selected_actors`

Returns the currently selected actors with label, class, location, and rotation.

### `get_world_outline`

Returns all actors in the level with parent-child hierarchy and folder info.

**Parameters:**

- `filter` (string): Optional substring filter on actor label
- `max_depth` (number): Ignored in v1 (flat list returned)

### `take_blueprint_preview_screenshot`

Open a Blueprint in its editor and capture the SCS (Components) viewport as a PNG screenshot

**Parameters:**

- `bp_path` (string, required): Blueprint asset path (e.g. /Game/Blueprints/BP_MyActor)
- `path` (string, required): Full file path for the PNG output (e.g. C:/temp/preview.png)

## PCG

### `create_pcg_graph`

Create an empty PCG Graph asset (Procedural Content Generation). Read it back with read_pcg_graph; drive it in a level via spawn_pcg_volume. Returns already_exists=true (not an error) if the asset is already present.

**Parameters:**

- `name` (string, required): Asset name (no extension)
- `path` (string, required): Content folder, e.g. /Game/PCG

### `read_pcg_graph`

Read a PCG Graph asset: input/output nodes plus every inner node (with its index, title, class, and input/output pin labels) and all edges (from_node.pin -> to_node.pin). Use the node 'index' values with connect_pcg_nodes. Read-only.

**Parameters:**

- `graph_path` (string, required): PCG Graph asset path, e.g. /Game/PCG/MyGraph

### `add_pcg_node`

Add an inner node to a PCG Graph by settings class (fuzzy name, e.g. 'SurfaceSampler', 'TransformPoints', 'StaticMeshSpawner', or full 'UPCGSurfaceSamplerSettings'). Returns the new node's index (use it with connect_pcg_nodes) plus its input/output pin labels. MUTATES the asset.

**Parameters:**

- `graph_path` (string, required): PCG Graph asset path
- `settings_class` (string, required): PCG settings class (fuzzy), e.g. SurfaceSampler / TransformPoints / StaticMeshSpawner / DensityFilter

### `add_pcg_subgraph_node`

Add a Subgraph node to a PCG Graph that executes another PCG Graph (composition/reuse). subgraph_path is the /Game PCG Graph to embed. Rejects self-reference. Returns the new node index + its input/output pins (mirrored from the subgraph's input/output nodes). MUTATES the asset.

**Parameters:**

- `graph_path` (string, required): Host PCG Graph asset path
- `subgraph_path` (string, required): PCG Graph to embed as a subgraph

### `set_pcg_node_property`

Set a property on a PCG node's Settings (e.g. SurfaceSampler PointsPerSquaredMeter, StaticMeshSpawner mesh). node = 'input'/'output' or an inner node index from read_pcg_graph. Use read_pcg_node to list settable properties + current values. MUTATES the asset.

**Parameters:**

- `graph_path` (string, required): PCG Graph asset path
- `node` (string, required): Node ref: inner node index (from read_pcg_graph)
- `property` (string, required): Property name on the node's Settings (see read_pcg_node)
- `value` (string, required): Value as string (imported via property reflection, e.g. '250', 'true', '(X=100,Y=100,Z=50)')

### `read_pcg_node`

Read a PCG node's Settings details: node class + all EditAnywhere properties (name, type, current value). node = 'input'/'output' or an inner node index. Read-only.

**Parameters:**

- `graph_path` (string, required): PCG Graph asset path
- `node` (string, required): Node ref: 'input'/'output' or an inner node index

### `connect_pcg_nodes`

Connect two nodes in a PCG Graph. Node refs are 'input'/'output' (the graph's I/O nodes) or an inner node INDEX from read_pcg_graph. Pin labels default to the source's first output pin and the target's first input pin (usually 'Out'->'In'); the graph input node's output pin is 'Input' and the output node's input pin is 'Output'. MUTATES the asset.

**Parameters:**

- `graph_path` (string, required): PCG Graph asset path
- `from_node` (string, required): Source node: 'input' or an inner node index
- `to_node` (string, required): Target node: 'output' or an inner node index
- `from_pin` (string): Source output pin label (default: first output pin)
- `to_pin` (string): Target input pin label (default: first input pin)

### `disconnect_pcg_nodes`

Remove an edge between two nodes in a PCG Graph. Node refs are 'input'/'output' or an inner node INDEX from read_pcg_graph. Pin labels default to the source's first output pin and the target's first input pin (as in connect_pcg_nodes). MUTATES the asset.

**Parameters:**

- `graph_path` (string, required): PCG Graph asset path
- `from_node` (string, required): Source node: 'input' or an inner node index
- `to_node` (string, required): Target node: 'output' or an inner node index
- `from_pin` (string): Source output pin label (default: first output pin)
- `to_pin` (string): Target input pin label (default: first input pin)

### `remove_pcg_node`

Remove an inner node (and its edges) from a PCG Graph by index (from read_pcg_graph). Cannot remove the graph 'input'/'output' nodes. Node indices shift after removal. MUTATES the asset.

**Parameters:**

- `graph_path` (string, required): PCG Graph asset path
- `node` (string, required): Inner node index (from read_pcg_graph)

### `find_pcg_graphs`

List PCG Graph assets under /Game (optionally filtered by a name substring). Read-only.

**Parameters:**

- `query` (string): Optional case-insensitive name filter substring

### `spawn_pcg_volume`

Spawn a PCG Volume actor in the current editor level, assign a PCG Graph to its PCG Component, and generate. MUTATES the level (spawns an actor). Needs an editor world (not during PIE).

**Parameters:**

- `graph_path` (string, required): PCG Graph asset path to assign, e.g. /Game/PCG/MyGraph
- `label` (string): Actor label (default 'PCG_Volume')
- `location` (object): Spawn location {x,y,z} (default origin)
- `scale` (object): Actor scale {x,y,z} (default {20,20,5})

### `pcg_generate`

Force-regenerate PCG on an existing PCG Volume actor (by label) in the current editor level.

**Parameters:**

- `actor` (string, required): Actor label of the PCG Volume

### `pcg_get_stats`

Read what a PCG Volume actor's generation actually produced — the assertion tool for PCG automation. Reports is_generating (Generate() is async: poll until false), the linked graph, output data (total point count + per-data breakdown), and spawned managed resources (spawned_actors, spawned_components, spawned_instances from ISM/HISM). Use after spawn_pcg_volume / pcg_generate. Read-only; needs an editor world.

**Parameters:**

- `actor` (string, required): PCG Volume actor label (as used by spawn_pcg_volume / pcg_generate)

### `read_pcg_params`

Read a PCG Graph's user parameters (the graph-level exposed inputs shown in its Details/Instance panel). Returns each parameter's name, type, and current value. Read-only.

**Parameters:**

- `graph_path` (string, required): PCG Graph asset path

### `add_pcg_param`

Add a user parameter (graph-level exposed input) to a PCG Graph. type = bool/int/int64/float/double/name/string/vector/rotator. Set its value with set_pcg_param. MUTATES the asset.

**Parameters:**

- `graph_path` (string, required): PCG Graph asset path
- `name` (string, required): New parameter name
- `type` (string, required): bool/int/int64/float/double/name/string/vector/rotator

### `set_pcg_param`

Set a PCG Graph user parameter's value by name (property-reflection import, e.g. '2.5', 'true', '(X=100,Y=0,Z=0)', or an asset path for object params). Use read_pcg_params to list parameters + types. MUTATES the asset.

**Parameters:**

- `graph_path` (string, required): PCG Graph asset path
- `name` (string, required): Parameter name (from read_pcg_params)
- `value` (string, required): Value as string

## PIE

### `pie_teleport_actor`

Teleport an actor in the PIE world

**Parameters:**

- `actor_label` (string, required): Actor label or name
- `location` (object, required): {x,y,z} target location
- `rotation` (object): Optional {pitch,yaw,roll} rotation

### `pie_spawn_actor`

Spawn an actor in the PIE world

**Parameters:**

- `class_path` (string, required): Blueprint or native class path
- `location` (object, required): {x,y,z} spawn location
- `rotation` (object): Optional {pitch,yaw,roll}
- `label` (string): Optional actor label

### `pie_destroy_actor`

Destroy an actor in the PIE world

**Parameters:**

- `actor_label` (string, required): Actor label or name

### `pie_get_property`

Get a property value from an actor via reflection

**Parameters:**

- `actor_label` (string, required): Actor label or name
- `property_name` (string, required): Property name

### `pie_set_property`

Set a property value on an actor via reflection

**Parameters:**

- `actor_label` (string, required): Actor label or name
- `property_name` (string, required): Property name
- `value` (string, required): Value as string (converted via property import)

### `pie_get_game_state`

Get PIE running state, player location, actor count

### `pie_list_actors`

List actors in the PIE world

**Parameters:**

- `class_filter` (string): Optional class name filter
- `name_filter` (string): Optional name substring filter

### `pie_console_command`

Execute a console command in the PIE world

**Parameters:**

- `command` (string, required): Console command string

### `pie_start`

Start a Play-In-Editor session

### `pie_stop`

Stop the active PIE session

### `pie_is_active`

Check whether a PIE session is currently active and return its mode

## Physics

### `create_physical_material`

Create a PhysicalMaterial with friction/restitution/density

**Parameters:**

- `name` (string, required): Physical material asset name
- `path` (string, required): Content folder path
- `friction` (number): Surface friction (default 0.7)
- `restitution` (number): Bounciness 0..1 (default 0.3)
- `density` (number): Density g/cm^3 (default 1.0)

### `read_physical_material`

Read a PhysicalMaterial's friction/restitution/density

**Parameters:**

- `asset_path` (string, required): Physical material asset path

## Project

### `get_project_info`

Returns basic project and engine information (name, version, directories).

### `list_plugins`

Lists all discovered plugins with name, version, enabled status, description, and category.

**Parameters:**

- `enabled_only` (boolean): If true, return only enabled plugins. Default: false.

### `create_folder`

Creates a content browser folder (e.g. /Game/MyFolder/SubFolder).

**Parameters:**

- `folder_path` (string, required): Content path starting with /Game/, e.g. /Game/MyFolder/SubFolder

### `get_source_files`

Lists source files recursively under a given path, filtered by extension.

**Parameters:**

- `path` (string): Filesystem path to search. Defaults to project Source directory.
- `extensions` (array): File extensions to include, e.g. [".h",".cpp"]. Defaults to both.

## RenderTarget

### `create_render_target`

Create a TextureRenderTarget2D

**Parameters:**

- `name` (string, required): Render target asset name
- `path` (string, required): Content folder path
- `width` (int): Width in pixels (default 256)
- `height` (int): Height in pixels (default 256)
- `format` (string): RGBA8 | RGBA16f | RGBA32f | R8 | R16f | R32f | RG8 | RG16f | RG32f (default RGBA8)

### `read_render_target`

Read a TextureRenderTarget2D: size and format

**Parameters:**

- `rt_path` (string, required): Render target asset path

## Sequencer

### `seq_create`

Create a LevelSequence asset

**Parameters:**

- `name` (string, required): Sequence asset name
- `path` (string, required): Content folder path (e.g. /Game/Cinematics)

### `seq_read`

Read sequence info (bindings, tracks, range)

**Parameters:**

- `sequence_path` (string, required): LevelSequence asset path

### `seq_add_binding`

Bind a world actor to a sequence

**Parameters:**

- `sequence_path` (string, required): LevelSequence asset path
- `actor_label` (string, required): Actor label in the world

### `seq_add_track`

Add a track to a binding (Transform, Float, Bool)

**Parameters:**

- `sequence_path` (string, required): LevelSequence asset path
- `binding_name` (string, required): Binding name in the sequence
- `track_type` (string, required): Track type: Transform, Float, Bool

### `seq_add_keyframe`

Add a keyframe to a track at a given time

**Parameters:**

- `sequence_path` (string, required): LevelSequence asset path
- `binding_name` (string, required): Binding name in the sequence
- `track_type` (string, required): Track type: Transform, Float, Bool
- `time` (float, required): Time in seconds for the keyframe
- `value` (object, required): Keyframe value. Transform: {lx,ly,lz,rx,ry,rz,sx,sy,sz}. Float: {value}. Bool: {value}

### `seq_set_range`

Set the playback range in frames

**Parameters:**

- `sequence_path` (string, required): LevelSequence asset path
- `start_frame` (int, required): Start frame number
- `end_frame` (int, required): End frame number

## System

### `ping`

Test server connectivity

### `list_tools`

List all available commands with schemas

**Parameters:**

- `category` (string): Optional category filter

### `get_protocol_info`

Get protocol and transport information

### `system_get_metrics`

Return current session command metrics

### `system_reset_metrics`

Reset all command metrics counters

## UMG

### `create_widget_blueprint`

Create a new Widget Blueprint asset

**Parameters:**

- `name` (string, required): Widget Blueprint asset name
- `path` (string, required): Content path, e.g. /Game/UI
- `rootWidget` (string): Root widget class (default: CanvasPanel)

### `read_widget_blueprint`

Read the widget tree of an existing Widget Blueprint

**Parameters:**

- `path` (string, required): Full asset path, e.g. /Game/UI/WBP_MyWidget

### `add_widget`

Add a widget to an existing Widget Blueprint tree. If a named parent panel is given, adds there; if the parent is not a valid panel, falls back to the root. The widget tree must already exist.

**Parameters:**

- `blueprint` (string, required): Full asset path to the Widget Blueprint
- `widgetClass` (string, required): Widget class: Button, TextBlock, Image, CanvasPanel, VerticalBox, HorizontalBox, Overlay, ScrollBox, Border, SizeBox
- `name` (string, required): Name for the new widget
- `parent` (string): Optional parent widget name; defaults to root panel

### `set_widget_property`

Set a property on a widget inside a Widget Blueprint via reflection

**Parameters:**

- `blueprint` (string, required): Full asset path to the Widget Blueprint
- `widget` (string, required): Widget name inside the blueprint
- `property` (string, required): Property name
- `value` (string, required): Value as string (imported via property reflection)

### `set_widget_slot_property`

Set a LAYOUT property on a widget's Slot (how the parent panel arranges it) via reflection. This is where layout lives — NOT on the widget itself. Examples: on a Canvas Panel slot set 'Offsets'='(Left=100,Top=50,Right=200,Bottom=80)', 'Anchors'='(Minimum=(X=0,Y=0),Maximum=(X=0,Y=0))', 'Alignment'='(X=0.5,Y=0.5)', 'ZOrder'; on a HorizontalBox/VerticalBox slot set 'Padding'='(Left=8,Top=4,Right=8,Bottom=4)', 'HorizontalAlignment'='HAlign_Center', 'Size'='(SizeRule=Fill,Value=1.0)'. The widget must already be parented (has a slot).

**Parameters:**

- `blueprint` (string, required): Full asset path to the Widget Blueprint
- `widget` (string, required): Widget name inside the blueprint
- `property` (string, required): Slot property name (Offsets/Anchors/Alignment/ZOrder/Padding/HorizontalAlignment/...)
- `value` (string, required): Value as string (property-reflection import)

### `remove_widget`

Remove a widget (and its children) from a Widget Blueprint tree by name. Cannot remove the root widget. MUTATES; recompiles on save.

**Parameters:**

- `blueprint` (string, required): Full asset path to the Widget Blueprint
- `widget` (string, required): Widget name to remove

### `bind_widget_event`

Bind a widget's multicast-delegate event (e.g. a Button's OnClicked) to a new event node in the Widget Blueprint's graph — creating the handler if absent. The widget is promoted to a variable automatically. Then author the handler with bp_create_node/bp_batch_op on the widget blueprint's EventGraph.

**Parameters:**

- `blueprint` (string, required): Full asset path to the Widget Blueprint
- `widget` (string, required): Widget name inside the blueprint (e.g. PlayBtn)
- `event` (string, required): Delegate/event name on the widget (e.g. OnClicked, OnHovered, OnValueChanged)

## Viewport

### `set_viewport_camera`

Set the active editor viewport camera location, rotation, and/or FOV

**Parameters:**

- `location` (object): Camera location {x,y,z}
- `rotation` (object): Camera rotation {pitch,yaw,roll}
- `fov` (number): Field of view in degrees

### `focus_on_actor`

Move the active viewport camera to focus on a named actor

**Parameters:**

- `actor_label` (string, required): Actor label or name

### `set_viewport_mode`

Set the active viewport projection mode (perspective or orthographic)

**Parameters:**

- `mode` (string, required): Viewport mode: perspective, top, bottom, left, right, front, back

### `get_viewport_info_detailed`

Get detailed active viewport info: camera, size, realtime state, view mode

### `select_actors`

Select one or more actors in the level by label

**Parameters:**

- `actor_labels` (array, required): Array of actor labels to select
- `add_to_selection` (boolean): If true, add to current selection; otherwise replace it

### `take_viewport_screenshot`

Capture the active editor viewport as a PNG file

**Parameters:**

- `path` (string, required): Full file path for the PNG output (e.g. C:/temp/shot.png)
