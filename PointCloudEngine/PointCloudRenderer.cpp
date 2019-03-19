#include "PointCloudRenderer.h"

std::vector<PointCloudRenderer*> PointCloudRenderer::sharedPointCloudRenderers;

PointCloudRenderer* PointCloudRenderer::CreateShared(std::wstring plyfile)
{
    PointCloudRenderer *pointCloudRenderer = new PointCloudRenderer(plyfile);
    pointCloudRenderer->shared = true;

    sharedPointCloudRenderers.push_back(pointCloudRenderer);
    return pointCloudRenderer;
}

void PointCloudRenderer::ReleaseAllSharedPointCloudRenderers()
{
    for (auto it = sharedPointCloudRenderers.begin(); it != sharedPointCloudRenderers.end(); it++)
    {
        PointCloudRenderer *pointCloudRenderer = *it;
        pointCloudRenderer->Release();
        delete pointCloudRenderer;
    }

    sharedPointCloudRenderers.clear();
}

PointCloudRenderer::PointCloudRenderer(std::wstring plyfile)
{
    // Load ply file
    std::ifstream ss(plyfile, std::ios::binary);

    PlyFile file;
    file.parse_header(ss);

    // Tinyply untyped byte buffers for properties
    std::shared_ptr<PlyData> rawPositions, rawNormals, rawColors;

    // Hardcoded properties and elements
    rawPositions = file.request_properties_from_element("vertex", { "x", "y", "z" });
    rawNormals = file.request_properties_from_element("vertex", { "nx", "ny", "nz" });
    rawColors = file.request_properties_from_element("vertex", { "red", "green", "blue", "alpha" });

    // Read the file
    file.read(ss);

    // Create vertices
    size_t count = rawPositions->count;
    size_t stridePositions = rawPositions->buffer.size_bytes() / count;
    size_t strideNormals = rawNormals->buffer.size_bytes() / count;
    size_t strideColors = rawColors->buffer.size_bytes() / count;
    vertices = std::vector<Vertex>(count);

    // Fill each vertex with its data
    for (int i = 0; i < count; i++)
    {
        std::memcpy(&vertices[i].position, rawPositions->buffer.get() + i * stridePositions, stridePositions);
        std::memcpy(&vertices[i].normal, rawNormals->buffer.get() + i * strideNormals, strideNormals);
        std::memcpy(&vertices[i].red, rawColors->buffer.get() + i * strideColors, strideColors);
    }

    // Set the default radius
    pointCloudConstantBufferData.radius = 0.02f;
}

void PointCloudRenderer::Initialize(SceneObject *sceneObject)
{
    // Create a vertex buffer description
    D3D11_BUFFER_DESC vertexBufferDesc;
    ZeroMemory(&vertexBufferDesc, sizeof(vertexBufferDesc));
    vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    vertexBufferDesc.ByteWidth = sizeof(Vertex) * vertices.size();
    vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vertexBufferDesc.CPUAccessFlags = 0;
    vertexBufferDesc.MiscFlags = 0;

    // Fill a D3D11_SUBRESOURCE_DATA struct with the data we want in the buffer
    D3D11_SUBRESOURCE_DATA vertexBufferData;
    ZeroMemory(&vertexBufferData, sizeof(vertexBufferData));
    vertexBufferData.pSysMem = &vertices[0];

    // Create the buffer
    hr = d3d11Device->CreateBuffer(&vertexBufferDesc, &vertexBufferData, &vertexBuffer);
    ErrorMessage(L"CreateBuffer failed for the vertex buffer.", L"Initialize", __FILEW__, __LINE__, hr);

    // Create the constant buffer for WVP
    D3D11_BUFFER_DESC cbDescWVP;
    ZeroMemory(&cbDescWVP, sizeof(cbDescWVP));
    cbDescWVP.Usage = D3D11_USAGE_DEFAULT;
    cbDescWVP.ByteWidth = sizeof(PointCloudConstantBuffer);
    cbDescWVP.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDescWVP.CPUAccessFlags = 0;
    cbDescWVP.MiscFlags = 0;

    hr = d3d11Device->CreateBuffer(&cbDescWVP, NULL, &pointCloudConstantBuffer);
    ErrorMessage(L"CreateBuffer failed for the constant buffer matrices.", L"Initialize", __FILEW__, __LINE__, hr);
}

void PointCloudRenderer::Update(SceneObject *sceneObject)
{
    if (Input::GetKey(Keyboard::Up))
    {
        pointCloudConstantBufferData.radius += dt * 0.01f;
    }
    else if (Input::GetKey(Keyboard::Down))
    {
        pointCloudConstantBufferData.radius -= dt * 0.01f;
    }

    pointCloudConstantBufferData.radius = max(0.0002f, pointCloudConstantBufferData.radius);
}

void PointCloudRenderer::Draw(SceneObject *sceneObject)
{
    // Set the shaders
    d3d11DevCon->VSSetShader(pointCloudShader->vertexShader, 0, 0);
    d3d11DevCon->GSSetShader(pointCloudShader->geometryShader, 0, 0);
    d3d11DevCon->PSSetShader(pointCloudShader->pixelShader, 0, 0);

    // Set the Input (Vertex) Layout
    d3d11DevCon->IASetInputLayout(pointCloudShader->inputLayout);

    // Bind the vertex buffer and index buffer to the input assembler (IA)
    UINT offset = 0;
    UINT stride = sizeof(Vertex);
    d3d11DevCon->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

    // Set primitive topology
    d3d11DevCon->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);

    // Set shader constant buffer variables
    pointCloudConstantBufferData.World = sceneObject->transform->worldMatrix.Transpose();
    pointCloudConstantBufferData.WorldInverseTranspose = pointCloudConstantBufferData.World.Invert().Transpose();
    pointCloudConstantBufferData.View = camera.view.Transpose();
    pointCloudConstantBufferData.Projection = camera.projection.Transpose();

    // Update effect file buffer, set shader buffer to our created buffer
    d3d11DevCon->UpdateSubresource(pointCloudConstantBuffer, 0, NULL, &pointCloudConstantBufferData, 0, 0);
    d3d11DevCon->VSSetConstantBuffers(0, 1, &pointCloudConstantBuffer);
    d3d11DevCon->GSSetConstantBuffers(0, 1, &pointCloudConstantBuffer);

    d3d11DevCon->Draw(vertices.size(), 0);
}

void PointCloudRenderer::Release()
{
    SafeRelease(vertexBuffer);
    SafeRelease(pointCloudConstantBuffer);
}
