#include "OctreeNode.h"

PointCloudEngine::OctreeNode::OctreeNode()
{
    // Default constructor used for parsing from file
}

PointCloudEngine::OctreeNode::OctreeNode(std::queue<OctreeNodeCreationEntry> &nodeCreationQueue, std::vector<OctreeNode> &nodes, const OctreeNodeCreationEntry &entry)
{
    size_t vertexCount = entry.vertices.size();
    
    if (vertexCount == 0)
    {
		ERROR_MESSAGE(L"Cannot create " + NAMEOF(OctreeNode) + L" from 0 " + NAMEOF(vertexCount));
        return;
    }

    // The octree is generated by fitting the vertices into a cube at the center position
    // Then this cube is splitted into 8 smaller child cubes along the center
    // For each child cube the octree generation is repeated
    // Assign node values given by the parent
    nodeVertex.size = entry.size;
    nodeVertex.position = entry.center;

    // Assign this child to its parent
    if (entry.parentIndex != UINT_MAX && entry.parentChildIndex >= 0)
    {
        nodes[entry.parentIndex].children[entry.parentChildIndex] = entry.nodeIndex;
    }

    // Apply the k-means clustering algorithm to find clusters for the normals
    Vector3 means[6];
    const int k = min(vertexCount, 6);
    UINT verticesPerMean[6] = { 0, 0, 0, 0, 0, 0 };

    // Set initial means to the first k normals
    for (int i = 0; i < k; i++)
    {
        means[i] = entry.vertices[i].normal;
        verticesPerMean[i] = 1;
    }

    // Save the index of the mean that each vertex is assigned to
    bool meanChanged = true;
    byte *clusters = new byte[vertexCount];
    ZeroMemory(clusters, sizeof(byte) * vertexCount);

    while (meanChanged)
    {
        // Assign all the vertices to the closest mean to them
        for (UINT i = 0; i < vertexCount; i++)
        {
            float minDistance = Vector3::Distance(entry.vertices[i].normal, means[clusters[i]]);

            for (UINT j = 0; j < k; j++)
            {
                float distance = Vector3::Distance(entry.vertices[i].normal, means[j]);

                if (distance < minDistance)
                {
                    clusters[i] = j;
                    minDistance = distance;
                }
            }
        }

        // Calculate the new means from the vertices in each cluster
        Vector3 newMeans[6];

        for (int i = 0; i < k; i++)
        {
            verticesPerMean[i] = 0;
        }

        for (UINT i = 0; i < vertexCount; i++)
        {
            newMeans[clusters[i]] += entry.vertices[i].normal;
            verticesPerMean[clusters[i]] += 1;
        }

        meanChanged = false;

        // Update the means
        for (int i = 0; i < k; i++)
        {
            if (verticesPerMean[i] > 0)
            {
                newMeans[i] /= verticesPerMean[i];

                if (Vector3::DistanceSquared(means[i], newMeans[i]) > FLT_EPSILON)
                {
                    meanChanged = true;
                }

                means[i] = newMeans[i];
            }
        }

    }

    // Initialize average colors that are calculated per cluster
    double averageReds[6] = { 0, 0, 0, 0, 0, 0 };
    double averageGreens[6] = { 0, 0, 0, 0, 0, 0 };
    double averageBlues[6] = { 0, 0, 0, 0, 0, 0 };

    // Calculate color
    for (UINT i = 0; i < vertexCount; i++)
    {
        averageReds[clusters[i]] += entry.vertices[i].color[0];
        averageGreens[clusters[i]] += entry.vertices[i].color[1];
        averageBlues[clusters[i]] += entry.vertices[i].color[2];
    }

    // Assign node vertex properties
    nodeVertex.weights = 0;

    for (int i = 0; i < 6; i++)
    {
        if (verticesPerMean[i] > 0)
        {
            averageReds[i] /= verticesPerMean[i];
            averageGreens[i] /= verticesPerMean[i];
            averageBlues[i] /= verticesPerMean[i];

            nodeVertex.normals[i] = PolarNormal(means[i]);
            nodeVertex.colors[i] = Color16(averageReds[i], averageGreens[i], averageBlues[i]);
            nodeVertex.weights |= static_cast<UINT>((31.0f * verticesPerMean[i]) / vertexCount) << (i * 5);
        }
    }

    delete[] clusters;

    // Split and create children vertices
    std::vector<Vertex> childVertices[8];

    // Fit each vertex into its corresponding child cube
    for (auto it = entry.vertices.begin(); it != entry.vertices.end(); it++)
    {
        Vertex v = *it;

        if (v.position.x > entry.center.x)
        {
            if (v.position.y > entry.center.y)
            {
                if (v.position.z > entry.center.z)
                {
                    childVertices[0].push_back(v);
                }
                else
                {
                    childVertices[1].push_back(v);
                }
            }
            else
            {
                if (v.position.z > entry.center.z)
                {
                    childVertices[2].push_back(v);
                }
                else
                {
                    childVertices[3].push_back(v);
                }
            }
        }
        else
        {
            if (v.position.y > entry.center.y)
            {
                if (v.position.z > entry.center.z)
                {
                    childVertices[4].push_back(v);
                }
                else
                {
                    childVertices[5].push_back(v);
                }
            }
            else
            {
                if (v.position.z > entry.center.z)
                {
                    childVertices[6].push_back(v);
                }
                else
                {
                    childVertices[7].push_back(v);
                }
            }
        }
    }

    // Assign the centers for each child cube
    float childExtend = 0.25f * nodeVertex.size;

    // Correlates to the assigned child vertices
    Vector3 childCenters[8] =
    {
        entry.center + Vector3(childExtend, childExtend, childExtend),
        entry.center + Vector3(childExtend, childExtend, -childExtend),
        entry.center + Vector3(childExtend, -childExtend, childExtend),
        entry.center + Vector3(childExtend, -childExtend, -childExtend),
        entry.center + Vector3(-childExtend, childExtend, childExtend),
        entry.center + Vector3(-childExtend, childExtend, -childExtend),
        entry.center + Vector3(-childExtend, -childExtend, childExtend),
        entry.center + Vector3(-childExtend, -childExtend, -childExtend)
    };

    // Only subdivide further if the size is above the minimum size
    if (entry.depth > 0)
    {
        for (int i = 0; i < 8; i++)
        {
            if (childVertices[i].size() > 0)
            {
                // Add a new entry to the queue
                OctreeNodeCreationEntry childEntry;
                childEntry.nodeIndex = UINT_MAX;
                childEntry.parentIndex = entry.nodeIndex;
                childEntry.parentChildIndex = i;
                childEntry.vertices = childVertices[i];
                childEntry.center = childCenters[i];
                childEntry.size = entry.size / 2.0f;
                childEntry.depth = entry.depth - 1;

                nodeCreationQueue.push(childEntry);
            }
        }
    }
}

void PointCloudEngine::OctreeNode::GetVertices(std::queue<UINT> &nodesQueue, std::vector<OctreeNodeVertex> &octreeVertices, const Vector3 &localCameraPosition, const float &splatSize) const
{
    // TODO: View frustum culling by checking the node bounding box against all the view frustum planes (don't check again if fully inside)
    // TODO: Visibility culling by comparing the maximum angle (normal cone) from the mean to all normals in the cluster against the view direction
    // Only return a vertex if its projected size is smaller than the passed size or it is a leaf node
    float distanceToCamera = Vector3::Distance(localCameraPosition, nodeVertex.position);

    // Scale the local space splat size by the fov and camera distance (Result: size at that distance in local space)
    float requiredSplatSize = splatSize * (2.0f * tan(settings->fovAngleY / 2.0f)) * distanceToCamera;

    if ((nodeVertex.size < requiredSplatSize) || IsLeafNode())
    {
        // Draw this vertex
        octreeVertices.push_back(nodeVertex);
    }
    else
    {
        // Traverse the child octrees
        for (int i = 0; i < 8; i++)
        {
            if (children[i] != UINT_MAX)
            {
                nodesQueue.push(children[i]);
            }
        }
    }
}

void PointCloudEngine::OctreeNode::GetVerticesAtLevel(std::queue<std::pair<UINT, int>> &nodesQueue, std::vector<OctreeNodeVertex> &octreeVertices, const int &level) const
{
    if (level == 0)
    {
        octreeVertices.push_back(nodeVertex);
    }
    else if (level > 0)
    {
        // Traverse the whole octree and add child vertices
        for (int i = 0; i < 8; i++)
        {
            if (children[i] != UINT_MAX)
            {
                nodesQueue.push(std::pair<UINT, int>(children[i], level - 1));
            }
        }
    }
}

bool PointCloudEngine::OctreeNode::IsLeafNode() const
{
    for (int i = 0; i < 8; i++)
    {
        if (children[i] != UINT_MAX)
        {
            return false;
        }
    }

    return true;
}
