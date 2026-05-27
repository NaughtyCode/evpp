#pragma once

#include <stddef.h>
#include <stdint.h>

#include "CullingEngineMacros.h"
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
#define CULLING_ENGINE_DEFAULT_ARG(value) = value
#else
#define CULLING_ENGINE_DEFAULT_ARG(value)
#endif

/*
 * Allocates memory for the top-level opaque engine handle when an embedding
 * application wants to track handle lifetime through its own allocator. Pass
 * nullptr to CullingEngineInit to use the engine default allocation path.
 *
 * Parameters:
 *   size - Number of bytes requested by the engine.
 *
 * Returns:
 *   Pointer to a block of at least size bytes, or nullptr on allocation
 * failure.
 */
typedef void* (*PFN_CullingEngineMalloc)(size_t size);

/*
 * Releases memory allocated by PFN_CullingEngineMalloc. The return type is kept
 * as void* for ABI compatibility with existing integrations.
 *
 * Parameters:
 *   ptr - Pointer previously returned by PFN_CullingEngineMalloc.
 *
 * Returns:
 *   Integration-defined value. The engine does not consume the return value.
 */
typedef void* (*PFN_CullingEngineFree)(void* ptr);

/*
 * Receives formatted log messages from the engine.
 *
 * Parameters:
 *   msg        - Null-terminated log message.
 *   filename   - Source file that emitted the message, when available.
 *   linenumber - Source line that emitted the message, when available.
 */
typedef void (*PFN_CullingEngineLogCallback)(const char* msg,
                                             const char* filename,
                                             int linenumber);

/*
 * Function pointer matching CullingEngineInit.
 */
typedef void* (*PFN_CullingEngineInit)(unsigned int width, unsigned int height,
                                       float nearPlane,
                                       PFN_CullingEngineMalloc mallocFunc,
                                       PFN_CullingEngineFree freeFunc);

/*
 * Function pointer matching CullingEngineStartNewFrame.
 */
typedef bool (*PFN_CullingEngineStartNewFrame)(void* pCullingEngine,
                                               const float* ViewPos,
                                               const float* ViewDir,
                                               const float* ViewProj,
                                               bool bRowMajorMat);

/*
 * Function pointer matching CullingEngineSync.
 */
typedef bool (*PFN_CullingEngineSync)(void* pCullingEngine, unsigned int id,
                                      void* param);

/*
 * Function pointer matching CullingEngineSet.
 */
typedef bool (*PFN_CullingEngineSet)(void* pCullingEngine, unsigned int ID,
                                     unsigned int configValue);

/*
 * Function pointer matching CullingEngineRenderOccluder.
 */
typedef void (*PFN_CullingEngineRenderOccluder)(
    void* pCullingEngine, const float* vertices, const unsigned short* indices,
    unsigned int nVert, unsigned int nIdx, const float* localToWorld,
    bool bRowMajorMat, bool enableBackfaceCull);

/*
 * Function pointer matching CullingEngineRenderBakedOccluder.
 */
typedef void (*PFN_CullingEngineRenderBakedOccluder)(
    void* pCullingEngine, unsigned short* compressedModel,
    const float* localToWorld, bool bRowMajorMat,
    int* outRasterizeTrianglesNum);

/*
 * Function pointer matching CullingEngineQueryOccludees.
 */
typedef bool (*PFN_CullingEngineQueryOccludees)(void* pCullingEngine,
                                                const float* bbox,
                                                unsigned int nMesh,
                                                bool* results);

/*
 * Function pointer matching CullingEngineMeshBake.
 */
typedef unsigned short* (*PFN_CullingEngineMeshBake)(
    void* occluderBakeBuffer, int* outputCompressSize, const float* vertices,
    const unsigned short* indices, unsigned int nVert, unsigned int nIdx,
    float quadAngle, bool enableBackfaceCull, bool counterClockWise,
    int squareTerrainAxisPoints);

/*
 * Function pointer matching CullingEngineCreateOccluderBakeBuffer.
 */
typedef void* (*PFN_CullingEngineCreateOccluderBakeBuffer)();

/*
 * Function pointer matching CullingEngineDestroyOccluderBakeBuffer.
 */
typedef void (*PFN_CullingEngineDestroyOccluderBakeBuffer)(
    void* pOccluderBakeBuffer);

/*
 * Function pointer matching CullingEngineSetIsDebugOccluderOccludee.
 */
typedef void (*PFN_CullingEngineSetIsDebugOccluderOccludee)(
    bool bIsDebugOccluderOccludee);

/*
 * Function pointer matching CullingEngineGetGetIsDebugOccluderOccludee.
 */
typedef bool (*PFN_CullingEngineGetGetIsDebugOccluderOccludee)();

/*
 * Function pointer matching CullingEngineSetNearPlane.
 */
typedef void (*PFN_CullingEngineSetNearPlane)(void* pCullingEngine,
                                              float nearPlane);

/*
 * Function pointer matching CullingEngineGetNearPlane.
 */
typedef float (*PFN_CullingEngineGetNearPlane)(void* pCullingEngine);

/*
 * Function pointer matching CullingEngineSetIsNeedCheckInFrustum.
 */
typedef void (*PFN_CullingEngineSetIsNeedCheckInFrustum)(
    void* pCullingEngine, bool bIsNeedCheckInFrustum);

/*
 * Function pointer matching CullingEngineGetIsNeedCheckInFrustum.
 */
typedef bool (*PFN_CullingEngineGetIsNeedCheckInFrustum)(void* pCullingEngine);

/*
 * Function pointer matching CullingEngineGetMemorySizeInBytes.
 */
typedef size_t (*PFN_CullingEngineGetMemorySizeInBytes)(void* pCullingEngine);

/*
 * Function pointer matching CullingEngineSetLogFunc.
 */
typedef void (*PFN_CullingEngineSetLogFunc)(
    PFN_CullingEngineLogCallback logFunc);

/*
 * Function pointer matching CullingEngineGetLatestCaptureDepthMapFilename.
 */
typedef const char* (*PFN_CullingEngineGetLatestCaptureDepthMapFilename)();

/*
 * Function pointer matching CullingEngineResetLatestCaptureDepthMapFilename.
 */
typedef void (*PFN_CullingEngineResetLatestCaptureDepthMapFilename)();

/*
 * Function pointer matching CullingEngineIsValidLatestCaptureDepthMapFilename.
 */
typedef bool (*PFN_CullingEngineIsValidLatestCaptureDepthMapFilename)();

/*
 * Function pointer matching CullingEngineGetLatestCapFilename.
 */
typedef const char* (*PFN_CullingEngineGetLatestCapFilename)();

/*
 * Function pointer matching CullingEngineResetLatestCapFilename.
 */
typedef void (*PFN_CullingEngineResetLatestCapFilename)();

/*
 * Function pointer matching CullingEngineIsValidLatestCapFilename.
 */
typedef bool (*PFN_CullingEngineIsValidLatestCapFilename)();

/*
 * Function pointer matching CullingEngineDumpOccluderOccludeeColorImage.
 */
typedef bool (*PFN_CullingEngineDumpOccluderOccludeeColorImage)(
    const char* filename, unsigned char* input, unsigned int width,
    unsigned int height);

#ifdef __cplusplus
extern "C" {
#endif
/*
 * Creates a CullingEngine instance and allocates its internal rasterizer
 * resources.
 *
 * Parameters:
 *   width      - Depth buffer width. It must be at least 64, no larger than
 *                65535, and divisible by 64.
 *   height     - Depth buffer height. It must be at least 8, no larger than
 *                65535, and divisible by 8.
 *   nearPlane  - Camera near clip distance. Values smaller than
 *                CULLING_ENGINE_MIN_NEAR_PLANE are clamped.
 *   mallocFunc - Optional custom allocator callback for the top-level
 *                opaque handle. Pass nullptr to use the default allocation
 *                path.
 *   freeFunc   - Optional custom free callback paired with mallocFunc. Pass
 *                either both callbacks or neither callback.
 *
 * Returns:
 *   Opaque engine handle on success, or nullptr when validation or
 *   initialization fails.
 *
 * Lifetime:
 *   Destroy the returned handle by calling CullingEngineSet with
 *   CULLING_ENGINE_DESTROY.
 */
CULLING_ENGINE_API void* CullingEngineInit(
    unsigned int width, unsigned int height, float nearPlane,
    PFN_CullingEngineMalloc mallocFunc CULLING_ENGINE_DEFAULT_ARG(nullptr),
    PFN_CullingEngineFree freeFunc CULLING_ENGINE_DEFAULT_ARG(nullptr));

/*
 * Begins a new frame and updates camera state used by coherent rendering.
 * Occluders must be submitted after this call and before occludee queries for
 * the frame.
 *
 * Parameters:
 *   pCullingEngine - Engine handle returned by CullingEngineInit.
 *   ViewPos        - Pointer to three floats storing camera world position.
 *   ViewDir        - Pointer to three floats storing camera view direction.
 *                    A zero direction marks a critical frame and forces the
 *                    engine to avoid coherent-frame shortcuts.
 *   ViewProj       - Pointer to sixteen floats storing the view-projection
 *                    matrix.
 *   bRowMajorMat   - True when ViewProj is row-major. False when the matrix
 *                    uses the engine default layout.
 *
 * Returns:
 *   true when the frame state was accepted, false when the handle or required
 *   pointers are invalid.
 */
CULLING_ENGINE_API bool CullingEngineStartNewFrame(void* pCullingEngine,
                                                   const float* ViewPos,
                                                   const float* ViewDir,
                                                   const float* ViewProj,
                                                   bool bRowMajorMat);

/*
 * Submits a raw triangle mesh as an occluder for the current frame.
 *
 * Parameters:
 *   pCullingEngine       - Engine handle returned by CullingEngineInit.
 *   vertices             - Vertex buffer laid out as XYZ float triples.
 *   indices              - Triangle index buffer. Every three indices form
 *                          one triangle.
 *   nVert                - Number of vertices in vertices.
 *   nIdx                 - Number of indices in indices. Must be nonzero and
 *                          divisible by 3.
 *   localToWorld         - Pointer to a 4x4 local-to-world transform. The
 *                          pointer is required even when mesh vertices are
 *                          already in world space.
 *   bRowMajorMat         - True when localToWorld is row-major.
 *   bEnableBackfaceCull  - True to enable back-face culling for this
 *                          occluder, false to rasterize both sides.
 *
 * Behavior:
 *   Invalid input is ignored. This function does not take ownership of input
 *   buffers; callers only need to keep them valid for the duration of the
 *   call.
 */
CULLING_ENGINE_API void CullingEngineRenderOccluder(
    void* pCullingEngine, const float* vertices, const unsigned short* indices,
    unsigned int nVert, unsigned int nIdx, const float* localToWorld,
    bool bRowMajorMat, bool bEnableBackfaceCull);

/*
 * Submits a pre-baked occluder generated by CullingEngineMeshBake.
 *
 * Parameters:
 *   pCullingEngine           - Engine handle returned by CullingEngineInit.
 *   compressedModel          - Baked occluder data returned by
 *                              CullingEngineMeshBake or copied from that
 *                              output.
 *   localToWorld             - Pointer to a 4x4 local-to-world transform.
 *   bRowMajorMat             - True when localToWorld is row-major.
 *   outRasterizeTrianglesNum - Optional output pointer receiving the number
 *                              of triangles emitted for rasterization.
 *
 * Behavior:
 *   Invalid input is ignored. The engine does not take ownership of
 *   compressedModel.
 */
CULLING_ENGINE_API void CullingEngineRenderBakedOccluder(
    void* pCullingEngine, unsigned short* compressedModel,
    const float* localToWorld, bool bRowMajorMat,
    int* outRasterizeTrianglesNum);

/*
 * Queries visibility for a batch of world-space axis-aligned bounding boxes.
 *
 * Parameters:
 *   pCullingEngine - Engine handle returned by CullingEngineInit.
 *   bbox           - Array of nMesh boxes. Each box is six floats in this
 *                    order: minX, minY, minZ, maxX, maxY, maxZ.
 *   nMesh          - Number of boxes in bbox. Must be greater than zero.
 *   results        - Output array with at least nMesh bool entries. A true
 *                    value means the corresponding occludee is visible or
 *                    conservatively treated as visible.
 *
 * Returns:
 *   true when the query ran, false when the handle or required buffers are
 *   invalid.
 */
CULLING_ENGINE_API bool CullingEngineQueryOccludees(void* pCullingEngine,
                                                    const float* bbox,
                                                    unsigned int nMesh,
                                                    bool* results);

/*
 * Updates a scalar engine configuration value or executes a simple engine
 * command.
 *
 * Parameters:
 *   pCullingEngine - Engine handle returned by CullingEngineInit.
 *   ID             - One CULLING_ENGINE_* configuration or command ID.
 *   configValue    - Value interpreted according to ID.
 *
 * Supported IDs:
 *   CULLING_ENGINE_SET_CCW
 *     configValue must be 0 or 1. Use 1 for counter-clockwise input meshes
 *     and 0 for clockwise meshes.
 *
 *   CULLING_ENGINE_SET_USE_PREV_DEPTH_BUFFER
 *     Reuses the previous frame depth buffer.
 *
 *   CULLING_ENGINE_SET_USE_PREV_FRAME_OCCLUDERS
 *     Reuses the first configValue occluders from the previous frame.
 *
 *   CULLING_ENGINE_RENDER_MODE
 *     Accepts CULLING_ENGINE_RENDER_MODE_FULL,
 *     CULLING_ENGINE_RENDER_MODE_COHERENT,
 *     CULLING_ENGINE_RENDER_MODE_COHERENT_FAST, or
 *     CULLING_ENGINE_RENDER_MODE_TOGGLE_RENDER_TYPE.
 *
 *   CULLING_ENGINE_SHOW_CULLED
 *     configValue must be 0 or 1. Use 1 to invert query visibility for
 *     debugging culled objects.
 *
 *   CULLING_ENGINE_SHOW_OCCLUDEE_IN_DEPTH_MAP
 *     configValue must be 0 or 1. Use 1 to draw occludee information into
 *     debug depth-map output.
 *
 *   CULLING_ENGINE_CAPTURE_FRAME
 *     Use configValue 1 to capture the next frame to the configured output
 *     directory.
 *
 *   CULLING_ENGINE_DESTROY
 *     Use configValue 1 to destroy the engine handle. The handle must not
 *     be used afterward.
 *
 *   CULLING_ENGINE_ENABLE_OCCLUDER_PRIORITY_QUEUE
 *     configValue must be 0 or 1. Use 1 to enable priority queue ordering for
 *     submitted occluders.
 *
 *   CULLING_ENGINE_BACK_FACE_CULL_OFF_OCCLUDER_FIRST
 *     configValue must be 0 or 1. Use 1 to render double-sided occluders
 *     before normal occluders.
 *
 *   CULLING_ENGINE_FLUSH_SUBMITTED_OCCLUDER
 *     Forces already submitted occluders to be rasterized.
 *
 * Returns:
 *   true when ID is supported and configValue is valid, otherwise false.
 */
CULLING_ENGINE_API bool CullingEngineSet(void* pCullingEngine, unsigned int ID,
                                         unsigned int configValue);

/*
 * Exchanges data with the engine through command IDs whose payloads are not a
 * single unsigned integer.
 *
 * Parameters:
 *   pCullingEngine - Engine handle returned by CullingEngineInit. Some
 *                    debug-only replay commands create their own temporary
 *                    engine and may ignore this handle.
 *   id             - One CULLING_ENGINE_* sync ID.
 *   param          - Input or output payload pointer. Its concrete type
 *                    depends on id.
 *
 * Supported IDs and payloads:
 *   CULLING_ENGINE_GET_MEMORY_USED
 *     param: int*. Receives memory usage in KiB.
 *
 *   CULLING_ENGINE_GET_DEPTH_BUFFER_WIDTH_HEIGHT
 *     param: int[2] or unsigned int[2]. Receives width and height.
 *
 *   CULLING_ENGINE_GET_DEPTH_MAP
 *     param: unsigned char*. Receives depth-map image data. The caller must
 *     provide enough storage for the active depth-buffer output.
 *
 *   CULLING_ENGINE_SAVE_DEPTH_MAP_PATH
 *     param: const char*. Sets the path used by the next
 *     CULLING_ENGINE_SAVE_DEPTH_MAP call.
 *
 *   CULLING_ENGINE_SAVE_DEPTH_MAP
 *     param: unsigned char*. Saves a color debug image using the previously
 *     configured path, or a generated path when no path is configured.
 *
 *   CULLING_ENGINE_GET_VERSION
 *     param: int*. Receives VERSION_MAJOR * 10 + VERSION_SUB on little-endian
 *     systems. Returns false on unsupported big-endian systems.
 *
 *   CULLING_ENGINE_SET_COHERENT_MODE_SMALL_ROTATE_DOT_ANGLE_THRESHOLD
 *     param: float*. Sets the small-rotation camera direction dot threshold.
 *
 *   CULLING_ENGINE_SET_COHERENT_MODE_LARGE_ROTATE_DOT_ANGLE_THRESHOLD
 *     param: float*. Sets the large-rotation camera direction dot threshold.
 *
 *   CULLING_ENGINE_SET_COHERENT_MODE_CAMERA_DISTANCE_NEAR_THRESHOLD
 *     param: float*. Sets the camera movement distance threshold used by
 *     coherent rendering.
 *
 *   CULLING_ENGINE_RESET_DEPTH_MAP_WIDTH_AND_HEIGHT
 *     param: unsigned int[2]. Requests a depth-buffer resize. Width follows
 *     the same constraints as CullingEngineInit: at least 64 and divisible
 *     by 64. Height must be at least 8 and divisible by 8.
 *
 *   CULLING_ENGINE_SET_PRINT_LOG_IN_GAME
 *     param: int*. Use 1 to store logs in the in-engine ring buffer, or 0 to
 *     send logs to the configured output path.
 *
 *   CULLING_ENGINE_GET_LOG
 *     param: char*. Receives one stored log message. The legacy buffer size
 *     expectation is 256 bytes.
 *
 *   CULLING_ENGINE_PRINT_LOG
 *     param: ignored. Flushes stored log messages to the configured log
 *     output.
 *
 *   CULLING_ENGINE_SET_FRAME_CAPTURE_OUTPUT_PATH
 *     param: char*. Sets the directory used by frame capture and debug mesh
 *     output.
 *
 *   CULLING_ENGINE_GET_IS_SAME_CAMERA
 *     param: bool*. Receives true when the current camera state matches the
 *     previous frame and the resolution did not change.
 *
 *   CULLING_ENGINE_BAKE_MESH_SIMPLIFY_CONFIG
 *     param: int[4]. Values configure mesh simplification during bake:
 *     [0] enables planar quad merge when nonzero,
 *     [1] terrain grid optimization mode in [0, 2],
 *     [2] rectangle angle in [80, 89],
 *     [3] rectangle merge angle in [1, 10].
 *
 *   CULLING_ENGINE_GET_OCCLUDER_POTENTIAL_VISIBLE_SET
 *     param: bool*. Receives the current occluder potential-visible set.
 *     The buffer must be large enough for all submitted occluders plus any
 *     sentinel data required by the rasterizer.
 *
 *   CULLING_ENGINE_SET_QUERY_TREE_DATA
 *     param: uint16_t*. Provides hierarchy metadata for occludee queries.
 *     A parent stores child count plus one, a child stores zero, and a normal
 *     standalone occludee stores one.
 *
 * Returns:
 *   true when the command succeeded, otherwise false.
 */
CULLING_ENGINE_API bool CullingEngineSync(void* pCullingEngine, unsigned int id,
                                          void* param);

/*
 * Bakes a raw occluder mesh into the engine's compact occluder format.
 *
 * Parameters:
 *   pOccluderBakeBuffer    - Bake buffer created by
 *                            CullingEngineCreateOccluderBakeBuffer.
 *   outputCompressSize     - Output pointer receiving the number of
 *                            unsigned short elements in the returned buffer.
 *   vertices               - Vertex buffer laid out as XYZ float triples.
 *   indices                - Triangle index buffer. Every three indices form
 *                            one triangle.
 *   nVert                  - Number of vertices in vertices.
 *   nIdx                   - Number of indices in indices. Must be nonzero
 *                            and divisible by 3.
 *   quadAngle              - Maximum normal-angle threshold used to merge
 *                            neighboring triangles into quad work. Valid
 *                            values are 0 or [1, 15]. Zero disables quad
 *                            drawing and mesh simplification.
 *   enableBackfaceCull     - True to bake the mesh as back-face culled.
 *   counterClockWise       - True when input triangle winding is
 *                            counter-clockwise.
 *   squareTerrainAxisPoints - For square-grid terrain, set this to the
 *                             number of points on each terrain axis. Use 0
 *                             for normal meshes.
 *
 * Returns:
 *   Pointer to compact baked data owned by pOccluderBakeBuffer, or nullptr
 *   on invalid input or unsupported platform. Copy the returned data before
 *   reusing or destroying the bake buffer.
 */
CULLING_ENGINE_API unsigned short* CullingEngineMeshBake(
    void* pOccluderBakeBuffer, int* outputCompressSize, const float* vertices,
    const unsigned short* indices, unsigned int nVert, unsigned int nIdx,
    float quadAngle, bool enableBackfaceCull, bool counterClockWise,
    int squareTerrainAxisPoints);

/*
 * Creates reusable temporary storage for CullingEngineMeshBake.
 *
 * Returns:
 *   Opaque bake-buffer handle, or nullptr if allocation fails.
 */
CULLING_ENGINE_API void* CullingEngineCreateOccluderBakeBuffer();

/*
 * Destroys a bake-buffer handle created by
 * CullingEngineCreateOccluderBakeBuffer.
 *
 * Parameters:
 *   pOccluderBakeBuffer - Bake-buffer handle to destroy. Passing nullptr is
 *                         ignored by the implementation.
 */
CULLING_ENGINE_API void CullingEngineDestroyOccluderBakeBuffer(
    void* pOccluderBakeBuffer);

/*
 * Enables or disables debug colorization for occluder and occludee output.
 *
 * Parameters:
 *   bIsDebugOccluderOccludee - True to enable debug colorization.
 */
CULLING_ENGINE_API void CullingEngineSetIsDebugOccluderOccludee(
    bool bIsDebugOccluderOccludee);

/*
 * Returns the current debug colorization flag used by occluder and occludee
 * output.
 */
CULLING_ENGINE_API bool CullingEngineGetGetIsDebugOccluderOccludee();

/*
 * Updates the near clip plane for an existing engine instance.
 *
 * Parameters:
 *   pCullingEngine - Engine handle returned by CullingEngineInit.
 *   nearPlane      - New near clip distance. Values smaller than
 *                    CULLING_ENGINE_MIN_NEAR_PLANE are clamped.
 */
CULLING_ENGINE_API void CullingEngineSetNearPlane(void* pCullingEngine,
                                                  float nearPlane);

/*
 * Returns the near clip plane currently stored by an engine instance.
 *
 * Parameters:
 *   pCullingEngine - Engine handle returned by CullingEngineInit.
 *
 * Returns:
 *   Current near clip distance, or CULLING_ENGINE_MIN_NEAR_PLANE when the
 *   handle is invalid.
 */
CULLING_ENGINE_API float CullingEngineGetNearPlane(void* pCullingEngine);

/*
 * Controls whether occludee queries perform frustum checks before occlusion
 * testing.
 *
 * Parameters:
 *   pCullingEngine          - Engine handle returned by CullingEngineInit.
 *   bIsNeedCheckInFrustum  - True to enable frustum checks, false to skip
 *                            them.
 */
CULLING_ENGINE_API void CullingEngineSetIsNeedCheckInFrustum(
    void* pCullingEngine, bool bIsNeedCheckInFrustum);

/*
 * Returns whether frustum checking is enabled for occludee queries.
 *
 * Parameters:
 *   pCullingEngine - Engine handle returned by CullingEngineInit.
 *
 * Returns:
 *   true when frustum checking is enabled, false when disabled or when the
 *   handle is invalid.
 */
CULLING_ENGINE_API bool CullingEngineGetIsNeedCheckInFrustum(
    void* pCullingEngine);

/*
 * Returns an approximate memory usage total for the engine instance.
 *
 * Parameters:
 *   pCullingEngine - Engine handle returned by CullingEngineInit.
 *
 * Returns:
 *   Memory usage in bytes, or 0 when the handle is invalid.
 */
CULLING_ENGINE_API size_t
CullingEngineGetMemorySizeInBytes(void* pCullingEngine);

/*
 * Installs a process-wide log callback.
 *
 * Parameters:
 *   logFunc - Callback invoked for future log messages. Passing nullptr
 *             disables the callback path.
 */
CULLING_ENGINE_API void CullingEngineSetLogFunc(
    PFN_CullingEngineLogCallback logFunc);

/*
 * Returns the most recent depth-map image filename produced by frame capture.
 *
 * Returns:
 *   Stable pointer to internal storage while the filename remains valid, or
 *   nullptr when no filename is available. Copy the string before resetting
 *   the filename or before engine shutdown.
 */
CULLING_ENGINE_API const char* CullingEngineGetLatestCaptureDepthMapFilename();

/*
 * Clears the stored latest depth-map capture filename.
 */
CULLING_ENGINE_API void CullingEngineResetLatestCaptureDepthMapFilename();

/*
 * Returns whether a latest depth-map capture filename is currently stored.
 */
CULLING_ENGINE_API bool CullingEngineIsValidLatestCaptureDepthMapFilename();

/*
 * Returns the most recent frame-capture .cap filename.
 *
 * Returns:
 *   Stable pointer to internal storage while the filename remains valid, or
 *   nullptr when no filename is available. Copy the string before resetting
 *   the filename or before engine shutdown.
 */
CULLING_ENGINE_API const char* CullingEngineGetLatestCapFilename();

/*
 * Clears the stored latest frame-capture .cap filename.
 */
CULLING_ENGINE_API void CullingEngineResetLatestCapFilename();

/*
 * Returns whether a latest frame-capture .cap filename is currently stored.
 */
CULLING_ENGINE_API bool CullingEngineIsValidLatestCapFilename();

/*
 * Saves a debug PNG image that combines depth data with occluder and occludee
 * metadata.
 *
 * Parameters:
 *   filename    - Output PNG path.
 *   inputBuffer - Buffer containing width * height grayscale depth bytes
 *                 followed by metadata used to draw occludee rectangles.
 *                 Engine-produced depth buffers reserve another width *
 *                 height bytes for that metadata area.
 *   width       - Image width in pixels.
 *   height      - Image height in pixels.
 *
 * Returns:
 *   true when the file was written, false when input validation or file I/O
 *   fails.
 */
CULLING_ENGINE_API bool CullingEngineDumpOccluderOccludeeColorImage(
    const char* filename, unsigned char* inputBuffer, unsigned int width,
    unsigned int height);

#ifdef __cplusplus
}
#endif

#undef CULLING_ENGINE_DEFAULT_ARG
