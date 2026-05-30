#ifndef TRINITY_PATH_COMPUTE_KERNEL_H
#define TRINITY_PATH_COMPUTE_KERNEL_H

#include "Define.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "MoveSplineInitArgs.h"
#include "PathGenerator.h"
#include <G3D/Vector3.h>

namespace Movement
{
    struct PathKernelInput
    {
        dtNavMesh const* navMesh = nullptr;
        dtNavMeshQuery const* navMeshQuery = nullptr;
        dtQueryFilter filter;
        G3D::Vector3 start = G3D::Vector3::zero();
        G3D::Vector3 end = G3D::Vector3::zero();
        bool forceDestination = false;
        bool straightLine = false;
        uint32 pointPathLimit = MAX_POINT_PATH_LENGTH;
    };

    struct PathKernelOutput
    {
        PathType type = PATHFIND_BLANK;
        PointsArray points;
        G3D::Vector3 endPosition = G3D::Vector3::zero();
    };

    // Z coordinates come from poly samples; callers requiring terrain-accurate
    // Z must run UpdateAllowedPositionZ on the main thread after Compute().
    class TC_GAME_API PathComputeKernel
    {
    public:
        PathComputeKernel();
        ~PathComputeKernel() = default;

        PathComputeKernel(PathComputeKernel const&) = delete;
        PathComputeKernel& operator=(PathComputeKernel const&) = delete;

        void Compute(PathKernelInput const& input, PathKernelOutput& output);

    private:
        dtPolyRef _polyBuf[MAX_PATH_LENGTH];
        uint32 _polyLength;

        static bool HaveTile(dtNavMesh const* navMesh, G3D::Vector3 const& p);
        // isOverPoly=false => closest poly was found in the box but the point
        // projects outside it (off-mesh, above a cliff, etc.).
        static dtPolyRef GetPolyByLocation(dtNavMeshQuery const* query, dtQueryFilter const* filter,
                                            float const* point, float* distance, bool* isOverPoly = nullptr);
        static bool InRangeYZX(float const* v1, float const* v2, float r, float h);

        void BuildShortcut(PathKernelInput const& input, PathKernelOutput& output);
        void BuildPolyPath(PathKernelInput const& input, PathKernelOutput& output,
                           dtPolyRef startPoly, dtPolyRef endPoly,
                           float const* startPoint, float const* endPoint);
        void BuildPointPath(PathKernelInput const& input, PathKernelOutput& output,
                            float const* startPoint, float const* endPoint);

        bool GetSteerTarget(dtNavMeshQuery const* query, dtQueryFilter const* filter,
                            float const* startPos, float const* endPos, float minTargetDist,
                            dtPolyRef const* path, uint32 pathSize,
                            float* steerPos, unsigned char& steerPosFlag, dtPolyRef& steerPosRef);
        uint32 FixupCorridor(dtPolyRef* path, uint32 npath, uint32 maxPath,
                              dtPolyRef const* visited, uint32 nvisited);
        dtStatus FindSmoothPath(dtNavMeshQuery const* query, dtQueryFilter const* filter,
                                 float const* startPos, float const* endPos,
                                 dtPolyRef const* polyPath, uint32 polyPathSize,
                                 float* smoothPath, int* smoothPathSize, uint32 smoothPathMaxSize);
    };
}

#endif
