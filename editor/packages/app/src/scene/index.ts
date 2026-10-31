/**
 * Scene builders: real visuals (terrain and object models with textures) for the editor viewport.
 *
 * Units: `SCENE_SCALE` (0.001) scene units per game unit, y up, like `LevelView`. Terrain cells are 2048 game units;
 * one model unit is 2048 game units (`MODEL_UNIT`). Everything exported here is three.js on the UI thread; decoding
 * runs in the worker (`decode.ts`, RPC `loadTerrain` / `loadModels` in rpc.ts).
 *
 * Typical use (integration points: docs/journal/2026-10-31/editor-rendering.md):
 *
 *   const cache = new TextureCache();
 *   const t = await call('loadTerrain', dirPath);                  // TerrainData
 *   const terrain = buildTerrain(t.layers, cache.add('terrain', t.textures));
 *   scene.add(terrain.group);
 *   level.setGround({ height: terrain.heightAt, meshes: [terrain.group] });   // GroundSource in level.ts
 *   const m = await call('loadModels', dirPath, classes);          // ModelData[]
 *   scene.add(buildInstancedModels(items));                        // one InstancedMesh per model
 *   classThumbnail(String(classId), model, lookup);                // data URL for the asset browser
 *   groundHeightAt(terrain, x, z, y);  raycastScene(camera, ndcX, ndcY, [terrain.group]);
 */
export { SCENE_SCALE, buildTerrain, layerHeightAt, type TerrainScene } from './terrain.ts';
export {
    MODEL_SCALE,
    MODEL_UNIT,
    buildInstancedModels,
    buildModel,
    clearModelCache,
    docInstanceMatrix,
    instanceMatrix,
    modelGeometry,
    modelMaterials,
    type DocPlacement,
    type ModelInstance,
} from './models.ts';
export { TextureCache, type TextureLookup } from './textures.ts';
export { classThumbnail, clearThumbnails } from './thumbnail.ts';
export { groundHeightAt, raycastScene, type SceneHit } from './pick.ts';
export type { ModelData, SceneTexture, TerrainData } from './decode.ts';
export { ModelLayer, type LayerObject } from './layer.ts';
export { HELPER_GROUPS, classesToLoad, helperGroupOf, partitionObjects, type HelperGroup } from './instances.ts';
export {
    clientToNdc,
    nearestHit,
    nearestScreenPoint,
    ndcToClient,
    pointsInRect,
    type ScreenPoint,
} from './pickmath.ts';
export { DEFAULT_VIEW_OPTIONS, LIGHTING, parseViewOptions, type ViewOptions } from './viewopts.ts';
