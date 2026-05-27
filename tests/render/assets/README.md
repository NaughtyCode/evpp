# Example Assets

Assets used by `CullingEngine_depth_scene_demo` are rooted here and grouped by
category:

- `scenes/`: JSON scene assets. Scene files describe render settings, camera
  state, categorized asset references, and scene instances.
- `models/`: OBJ mesh assets referenced by scene JSON through
  `assets.models[].file`.

Scene asset paths are resolved relative to `assetRoot`, then through the
category directory. For example, a model entry with `"file": "spot.obj"` is
loaded from `<assetRoot>/models/spot.obj`.
