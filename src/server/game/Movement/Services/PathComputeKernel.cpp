#include "PathComputeKernel.h"
#include "DetourCommon.h"
#include <cstring>

namespace Movement
{
    PathComputeKernel::PathComputeKernel()
        : _polyLength(0)
    {
        std::memset(_polyBuf, 0, sizeof(_polyBuf));
    }

    bool PathComputeKernel::HaveTile(dtNavMesh const* navMesh, G3D::Vector3 const& p)
    {
        int tx = -1, ty = -1;
        float point[VERTEX_SIZE] = { p.y, p.z, p.x };
        navMesh->calcTileLoc(point, &tx, &ty);
        if (tx < 0 || ty < 0)
            return false;
        return navMesh->getTileAt(tx, ty, 0) != nullptr;
    }

    bool PathComputeKernel::InRangeYZX(float const* v1, float const* v2, float r, float h)
    {
        float const dx = v2[0] - v1[0];
        float const dy = v2[1] - v1[1];
        float const dz = v2[2] - v1[2];
        return (dx * dx + dz * dz) < r * r && std::fabs(dy) < h;
    }

    dtPolyRef PathComputeKernel::GetPolyByLocation(dtNavMeshQuery const* query, dtQueryFilter const* filter, float const* point, float* distance, bool* isOverPoly)
    {
        float extents[VERTEX_SIZE] = { 3.0f, 5.0f, 3.0f };
        float closestPoint[VERTEX_SIZE] = { 0.0f, 0.0f, 0.0f };
        dtPolyRef polyRef = INVALID_POLYREF;
        bool overPoly = false;
        if (dtStatusSucceed(query->findNearestPoly(point, extents, filter, &polyRef, closestPoint, &overPoly)) && polyRef != INVALID_POLYREF)
        {
            *distance = dtVdist(closestPoint, point);
            if (isOverPoly) *isOverPoly = overPoly;
            return polyRef;
        }
        // Bigger search box; capped at 50y elevation to bound the search.
        extents[1] = 50.0f;
        if (dtStatusSucceed(query->findNearestPoly(point, extents, filter, &polyRef, closestPoint, &overPoly)) && polyRef != INVALID_POLYREF)
        {
            *distance = dtVdist(closestPoint, point);
            if (isOverPoly) *isOverPoly = overPoly;
            return polyRef;
        }
        if (isOverPoly) *isOverPoly = false;
        return INVALID_POLYREF;
    }

    void PathComputeKernel::BuildShortcut(PathKernelInput const& input, PathKernelOutput& output)
    {
        output.points.clear();
        output.points.resize(2);
        output.points[0] = input.start;
        output.points[1] = input.end;
        output.endPosition = input.end;
    }

    uint32 PathComputeKernel::FixupCorridor(dtPolyRef* path, uint32 npath, uint32 maxPath, dtPolyRef const* visited, uint32 nvisited)
    {
        int32 furthestPath = -1;
        int32 furthestVisited = -1;

        for (int32 i = int32(npath) - 1; i >= 0; --i)
        {
            bool found = false;
            for (int32 j = int32(nvisited) - 1; j >= 0; --j)
            {
                if (path[i] == visited[j])
                {
                    furthestPath = i;
                    furthestVisited = j;
                    found = true;
                }
            }
            if (found)
                break;
        }

        if (furthestPath == -1 || furthestVisited == -1)
            return npath;

        uint32 req = nvisited - furthestVisited;
        uint32 orig = uint32(furthestPath + 1) < npath ? furthestPath + 1 : npath;
        uint32 size = npath > orig ? npath - orig : 0;
        if (req + size > maxPath)
            size = maxPath - req;

        if (size)
            std::memmove(path + req, path + orig, size * sizeof(dtPolyRef));

        for (uint32 i = 0; i < req; ++i)
            path[i] = visited[(nvisited - 1) - i];

        return req + size;
    }

    bool PathComputeKernel::GetSteerTarget(dtNavMeshQuery const* query, dtQueryFilter const* filter, float const* startPos, float const* endPos, float minTargetDist, dtPolyRef const* path, uint32 pathSize, float* steerPos, unsigned char& steerPosFlag, dtPolyRef& steerPosRef)
    {
        static constexpr uint32 MAX_STEER_POINTS = 3;
        float steerPath[MAX_STEER_POINTS * VERTEX_SIZE];
        unsigned char steerPathFlags[MAX_STEER_POINTS];
        dtPolyRef steerPathPolys[MAX_STEER_POINTS];
        uint32 nsteerPath = 0;
        dtStatus dtResult = query->findStraightPath(startPos, endPos, path, pathSize, steerPath, steerPathFlags, steerPathPolys, reinterpret_cast<int*>(&nsteerPath), MAX_STEER_POINTS);
        if (!nsteerPath || dtStatusFailed(dtResult))
            return false;

        uint32 ns = 0;
        while (ns < nsteerPath)
        {
            if ((steerPathFlags[ns] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) ||
                !InRangeYZX(&steerPath[ns * VERTEX_SIZE], startPos, minTargetDist, 1000.0f))
                break;
            ns++;
        }
        if (ns >= nsteerPath)
            return false;

        dtVcopy(steerPos, &steerPath[ns * VERTEX_SIZE]);
        steerPos[1] = startPos[1];
        steerPosFlag = steerPathFlags[ns];
        steerPosRef = steerPathPolys[ns];

        (void)filter;
        return true;
    }

    dtStatus PathComputeKernel::FindSmoothPath(dtNavMeshQuery const* query, dtQueryFilter const* filter, float const* startPos, float const* endPos, dtPolyRef const* polyPath, uint32 polyPathSize, float* smoothPath, int* smoothPathSize, uint32 maxSmoothPathSize)
    {
        *smoothPathSize = 0;
        uint32 nsmoothPath = 0;

        dtPolyRef polys[MAX_PATH_LENGTH];
        std::memcpy(polys, polyPath, sizeof(dtPolyRef) * polyPathSize);
        uint32 npolys = polyPathSize;

        float iterPos[VERTEX_SIZE];
        float targetPos[VERTEX_SIZE];
        if (dtStatusFailed(query->closestPointOnPolyBoundary(polys[0], startPos, iterPos)))
            return DT_FAILURE;
        if (dtStatusFailed(query->closestPointOnPolyBoundary(polys[npolys - 1], endPos, targetPos)))
            return DT_FAILURE;

        dtVcopy(&smoothPath[nsmoothPath * VERTEX_SIZE], iterPos);
        nsmoothPath++;

        while (npolys && nsmoothPath < maxSmoothPathSize)
        {
            float steerPos[VERTEX_SIZE];
            unsigned char steerPosFlag;
            dtPolyRef steerPosRef = INVALID_POLYREF;

            if (!GetSteerTarget(query, filter, iterPos, targetPos, SMOOTH_PATH_SLOP, polys, npolys, steerPos, steerPosFlag, steerPosRef))
                break;

            bool endOfPath = (steerPosFlag & DT_STRAIGHTPATH_END) != 0;
            bool offMeshConnection = (steerPosFlag & DT_STRAIGHTPATH_OFFMESH_CONNECTION) != 0;

            float delta[VERTEX_SIZE];
            dtVsub(delta, steerPos, iterPos);
            float len = dtMathSqrtf(dtVdot(delta, delta));
            if ((endOfPath || offMeshConnection) && len < SMOOTH_PATH_STEP_SIZE)
                len = 1.0f;
            else
                len = SMOOTH_PATH_STEP_SIZE / len;

            float moveTgt[VERTEX_SIZE];
            dtVmad(moveTgt, iterPos, delta, len);

            float result[VERTEX_SIZE];
            static constexpr uint32 MAX_VISIT_POLY = 16;
            dtPolyRef visited[MAX_VISIT_POLY];
            uint32 nvisited = 0;
            query->moveAlongSurface(polys[0], iterPos, moveTgt, filter, result, visited, reinterpret_cast<int*>(&nvisited), MAX_VISIT_POLY);
            npolys = FixupCorridor(polys, npolys, MAX_PATH_LENGTH, visited, nvisited);

            query->getPolyHeight(polys[0], result, &result[1]);
            result[1] += 0.5f;
            dtVcopy(iterPos, result);

            if (endOfPath && InRangeYZX(iterPos, steerPos, SMOOTH_PATH_SLOP, 1.0f))
            {
                dtVcopy(iterPos, targetPos);
                if (nsmoothPath < maxSmoothPathSize)
                {
                    dtVcopy(&smoothPath[nsmoothPath * VERTEX_SIZE], iterPos);
                    nsmoothPath++;
                }
                break;
            }

            if (nsmoothPath < maxSmoothPathSize)
            {
                dtVcopy(&smoothPath[nsmoothPath * VERTEX_SIZE], iterPos);
                nsmoothPath++;
            }
        }

        *smoothPathSize = nsmoothPath;
        return nsmoothPath < MAX_POINT_PATH_LENGTH ? DT_SUCCESS : DT_FAILURE;
    }

    void PathComputeKernel::BuildPolyPath(PathKernelInput const& input, PathKernelOutput& output, dtPolyRef startPoly, dtPolyRef endPoly, float const* startPoint, float const* endPoint)
    {
        if (startPoly == endPoly)
        {
            _polyBuf[0] = startPoly;
            _polyLength = 1;
            output.type = PATHFIND_NORMAL;
            return;
        }

        dtStatus dtResult;
        if (input.straightLine)
        {
            float hit = 0.0f;
            float hitNormal[VERTEX_SIZE] = { 0.0f, 0.0f, 0.0f };
            uint32 nResultPath = 0;
            dtResult = input.navMeshQuery->raycast(startPoly, startPoint, endPoint, &input.filter, &hit, hitNormal,
                                                    _polyBuf, reinterpret_cast<int*>(&nResultPath), MAX_PATH_LENGTH);
            _polyLength = nResultPath;
            if (dtStatusFailed(dtResult) || _polyLength == 0)
            {
                output.type = PATHFIND_NOPATH;
                return;
            }
            output.type = (hit == FLT_MAX) ? PATHFIND_NORMAL : PATHFIND_INCOMPLETE;
        }
        else
        {
            uint32 nResultPath = 0;
            dtResult = input.navMeshQuery->findPath(startPoly, endPoly, startPoint, endPoint, &input.filter,
                                                     _polyBuf, reinterpret_cast<int*>(&nResultPath), MAX_PATH_LENGTH);
            _polyLength = nResultPath;
            if (dtStatusFailed(dtResult) || _polyLength == 0)
            {
                output.type = PATHFIND_NOPATH;
                return;
            }
            // Without the truncated check a very long path can read as NORMAL
            // because its tail still ends on a valid poly.
            bool const truncated = (dtResult & (DT_OUT_OF_NODES | DT_PARTIAL_RESULT)) != 0;
            bool const reachedEnd = _polyBuf[_polyLength - 1] == endPoly;
            output.type = (reachedEnd && !truncated) ? PATHFIND_NORMAL : PATHFIND_INCOMPLETE;
        }
    }

    void PathComputeKernel::BuildPointPath(PathKernelInput const& input, PathKernelOutput& output, float const* startPoint, float const* endPoint)
    {
        float pathPoints[MAX_POINT_PATH_LENGTH * VERTEX_SIZE];
        uint32 pointCount = 0;
        dtStatus dtResult;

        if (input.straightLine)
        {
            dtResult = DT_SUCCESS;
            pointCount = 1;
            std::memcpy(&pathPoints[VERTEX_SIZE * 0], startPoint, sizeof(float) * 3);

            // Walk the poly buffer and pull straight line vertices.
            for (uint32 i = 0; i < _polyLength && pointCount < input.pointPathLimit; ++i)
            {
                float hit;
                float hitNormal[3];
                if (dtStatusFailed(input.navMeshQuery->raycast(_polyBuf[i], &pathPoints[(pointCount - 1) * VERTEX_SIZE], endPoint, &input.filter, &hit, hitNormal, nullptr, nullptr, 0)))
                {
                    output.type = PATHFIND_NOPATH;
                    return;
                }
                if (hit < 1.0f)
                {
                    float* p = &pathPoints[pointCount * VERTEX_SIZE];
                    p[0] = pathPoints[(pointCount - 1) * VERTEX_SIZE + 0] + hit * (endPoint[0] - pathPoints[(pointCount - 1) * VERTEX_SIZE + 0]);
                    p[1] = pathPoints[(pointCount - 1) * VERTEX_SIZE + 1] + hit * (endPoint[1] - pathPoints[(pointCount - 1) * VERTEX_SIZE + 1]);
                    p[2] = pathPoints[(pointCount - 1) * VERTEX_SIZE + 2] + hit * (endPoint[2] - pathPoints[(pointCount - 1) * VERTEX_SIZE + 2]);
                    pointCount++;
                    break;
                }
            }

            if (pointCount < input.pointPathLimit)
            {
                std::memcpy(&pathPoints[pointCount * VERTEX_SIZE], endPoint, sizeof(float) * 3);
                pointCount++;
            }
        }
        else
        {
            int smoothPathSize = 0;
            dtResult = FindSmoothPath(input.navMeshQuery, &input.filter, startPoint, endPoint, _polyBuf, _polyLength, pathPoints, &smoothPathSize, input.pointPathLimit);
            pointCount = uint32(smoothPathSize);
        }

        if (pointCount < 2 || dtStatusFailed(dtResult))
        {
            output.type = PATHFIND_NOPATH;
            output.points.clear();
            return;
        }

        if (pointCount == input.pointPathLimit)
            output.type = PathType(uint32(output.type) | uint32(PATHFIND_SHORT));

        output.points.resize(pointCount);
        for (uint32 i = 0; i < pointCount; ++i)
            output.points[i] = G3D::Vector3(pathPoints[i * VERTEX_SIZE + 2], pathPoints[i * VERTEX_SIZE + 0], pathPoints[i * VERTEX_SIZE + 1]);

        output.endPosition = output.points.back();
    }

    void PathComputeKernel::Compute(PathKernelInput const& input, PathKernelOutput& output)
    {
        output.points.clear();
        output.type = PATHFIND_BLANK;
        output.endPosition = input.end;
        _polyLength = 0;

        if (!input.navMesh || !input.navMeshQuery)
        {
            BuildShortcut(input, output);
            output.type = PathType(PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH);
            return;
        }

        if (!HaveTile(input.navMesh, input.start) || !HaveTile(input.navMesh, input.end))
        {
            BuildShortcut(input, output);
            output.type = PathType(PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH);
            return;
        }

        float startPoint[VERTEX_SIZE] = { input.start.y, input.start.z, input.start.x };
        float endPoint[VERTEX_SIZE]   = { input.end.y,   input.end.z,   input.end.x };

        float distToStart, distToEnd;
        bool startOver = false, endOver = false;
        dtPolyRef startPoly = GetPolyByLocation(input.navMeshQuery, &input.filter, startPoint, &distToStart, &startOver);
        dtPolyRef endPoly   = GetPolyByLocation(input.navMeshQuery, &input.filter, endPoint,   &distToEnd,   &endOver);

        if (startPoly == INVALID_POLYREF || endPoly == INVALID_POLYREF)
        {
            BuildShortcut(input, output);
            output.type = PATHFIND_NOPATH;
            return;
        }

        // !isOverPoly catches points that match the closest poly but project
        // outside it (off-mesh, above a cliff, beyond a tile border).
        bool const farFromPoly = (distToStart > 7.0f || distToEnd > 7.0f) || !startOver || !endOver;

        BuildPolyPath(input, output, startPoly, endPoly, startPoint, endPoint);
        if (output.type & PATHFIND_NOPATH)
            return;

        if (farFromPoly)
        {
            float closestPoint[VERTEX_SIZE];
            if (dtStatusSucceed(input.navMeshQuery->closestPointOnPoly(endPoly, endPoint, closestPoint, nullptr)))
                std::memcpy(endPoint, closestPoint, sizeof(closestPoint));
            output.type = PATHFIND_INCOMPLETE;
        }

        BuildPointPath(input, output, startPoint, endPoint);

        if (!input.forceDestination && (output.type & PATHFIND_INCOMPLETE) && !farFromPoly)
            output.type = PathType(uint32(output.type) | uint32(PATHFIND_NOPATH));
    }
}
