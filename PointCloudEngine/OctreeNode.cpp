#include "OctreeNode.h"

PointCloudEngine::OctreeNode::OctreeNode(const std::vector<Vertex> &vertices, const Vector3 &center, const float &size, const int &depth)
{
    size_t vertexCount = vertices.size();
    
    if (vertexCount == 0)
    {
        ErrorMessage(L"Cannot create Octree Node from empty vertices!", L"CreateNode", __FILEW__, __LINE__);
        return;
    }

    // The octree is generated by fitting the vertices into a cube at the center position
    // Then this cube is splitted into 8 smaller child cubes along the center
    // For each child cube the octree generation is repeated
    // Assign node values given by the parent
    nodeVertex.size = size;
    nodeVertex.position = center;

    // Apply the k-means clustering algorithm to find clusters for the normals
    Vector3 means[6];
    const int k = min(vertexCount, 6);
    int verticesPerMean[6] = { 0, 0, 0, 0, 0, 0 };

    // Set initial means to the first k normals
    for (int i = 0; i < k; i++)
    {
        means[i] = vertices[i].normal;
        verticesPerMean[i] = 1;
    }

    // Save the index of the mean that each vertex is assigned to
    bool meanChanged = true;
    byte *clusters = new byte[vertexCount];
    ZeroMemory(clusters, sizeof(byte) * vertexCount);

    while (meanChanged)
    {
        // Assign all the vertices to the closest mean to them
        for (int i = 0; i < vertexCount; i++)
        {
            float minDistance = Vector3::Distance(vertices[i].normal, means[clusters[i]]);

            for (int j = 0; j < k; j++)
            {
                float distance = Vector3::Distance(vertices[i].normal, means[j]);

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

        for (int i = 0; i < vertexCount; i++)
        {
            newMeans[clusters[i]] += vertices[i].normal;
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
    for (int i = 0; i < vertexCount; i++)
    {
        averageReds[clusters[i]] += vertices[i].color[0];
        averageGreens[clusters[i]] += vertices[i].color[1];
        averageBlues[clusters[i]] += vertices[i].color[2];
    }

    // Assign node vertex properties
    for (int i = 0; i < 6; i++)
    {
        if (verticesPerMean[i] > 0)
        {
            averageReds[i] /= verticesPerMean[i];
            averageGreens[i] /= verticesPerMean[i];
            averageBlues[i] /= verticesPerMean[i];

            nodeVertex.normals[i] = PolarNormal(means[i]);
            nodeVertex.colors[i] = Color16(averageReds[i], averageGreens[i], averageBlues[i]);
            nodeVertex.weights[i] = (255.0f * verticesPerMean[i]) / vertexCount;
        }
        else
        {
            nodeVertex.weights[i] = 0;
        }
    }

    delete[] clusters;

    // Split and create children vertices
    std::vector<Vertex> childVertices[8];

    // Fit each vertex into its corresponding child cube
    for (auto it = vertices.begin(); it != vertices.end(); it++)
    {
        Vertex v = *it;

        if (v.position.x > center.x)
        {
            if (v.position.y > center.y)
            {
                if (v.position.z > center.z)
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
                if (v.position.z > center.z)
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
            if (v.position.y > center.y)
            {
                if (v.position.z > center.z)
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
                if (v.position.z > center.z)
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
        center + Vector3(childExtend, childExtend, childExtend),
        center + Vector3(childExtend, childExtend, -childExtend),
        center + Vector3(childExtend, -childExtend, childExtend),
        center + Vector3(childExtend, -childExtend, -childExtend),
        center + Vector3(-childExtend, childExtend, childExtend),
        center + Vector3(-childExtend, childExtend, -childExtend),
        center + Vector3(-childExtend, -childExtend, childExtend),
        center + Vector3(-childExtend, -childExtend, -childExtend)
    };

    // Only subdivide further if the size is above the minimum size
    if (depth > 0)
    {
        for (int i = 0; i < 8; i++)
        {
            if (childVertices[i].size() > 0)
            {
                children[i] = new OctreeNode(childVertices[i], childCenters[i], size / 2.0f, depth - 1);
            }
        }
    }
}

PointCloudEngine::OctreeNode::~OctreeNode()
{
    // Delete children
    for (int i = 0; i < 8; i++)
    {
        SafeDelete(children[i]);
    }
}

std::vector<OctreeNodeVertex> PointCloudEngine::OctreeNode::GetVertices(const Vector3 &localCameraPosition, const float &splatSize)
{
    // TODO: View frustum culling by checking the node bounding box against all the view frustum planes (don't check again if fully inside)
    // TODO: Visibility culling by comparing the maximum angle (normal cone) from the mean to all normals in the cluster against the view direction
    // Only return a vertex if its projected size is smaller than the passed size or it is a leaf node
    std::vector<OctreeNodeVertex> octreeVertices;
    float distanceToCamera = Vector3::Distance(localCameraPosition, nodeVertex.position);

    // Scale the local space splat size by the fov and camera distance (Result: size at that distance in local space)
    float requiredSplatSize = splatSize * (2.0f * tan(settings->fovAngleY / 2.0f)) * distanceToCamera;

    if ((nodeVertex.size < requiredSplatSize) || IsLeafNode())
    {
        // Make sure that e.g. single point nodes with size 0 are drawn as well
        if (nodeVertex.size < FLT_EPSILON)
        {
            // Set the size temporarily to the splat size in local space to make sure that this node is visible
            OctreeNodeVertex tmp = nodeVertex;
            tmp.size = requiredSplatSize;

            octreeVertices.push_back(tmp);
        }
        else
        {
            octreeVertices.push_back(nodeVertex);
        }
    }
    else
    {
        // Traverse the whole octree and add child vertices
        for (int i = 0; i < 8; i++)
        {
            if (children[i] != NULL)
            {
                std::vector<OctreeNodeVertex> childOctreeVertices = children[i]->GetVertices(localCameraPosition, splatSize);
                octreeVertices.insert(octreeVertices.end(), childOctreeVertices.begin(), childOctreeVertices.end());
            }
        }
    }

    return octreeVertices;
}

std::vector<OctreeNodeVertex> PointCloudEngine::OctreeNode::GetVerticesAtLevel(const int &level)
{
    std::vector<OctreeNodeVertex> octreeVertices;

    if (level == 0)
    {
        octreeVertices.push_back(nodeVertex);
    }
    else if (level > 0)
    {
        // Traverse the whole octree and add child vertices
        for (int i = 0; i < 8; i++)
        {
            if (children[i] != NULL)
            {
                std::vector<OctreeNodeVertex> childOctreeVertices = children[i]->GetVerticesAtLevel(level - 1);
                octreeVertices.insert(octreeVertices.end(), childOctreeVertices.begin(), childOctreeVertices.end());
            }
        }
    }

    return octreeVertices;
}

bool PointCloudEngine::OctreeNode::IsLeafNode()
{
    for (int i = 0; i < 8; i++)
    {
        if (children[i] != NULL)
        {
            return false;
        }
    }

    return true;
}
