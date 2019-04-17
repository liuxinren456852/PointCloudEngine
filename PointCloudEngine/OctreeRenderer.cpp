#include "OctreeRenderer.h"

OctreeRenderer::OctreeRenderer(const std::wstring &plyfile)
{
    // Create the octree, throws exception on fail
    octree = new Octree(plyfile);

    // Text for showing properties
    text = Hierarchy::Create(L"OctreeRendererText");
    textRenderer = text->AddComponent(new TextRenderer(TextRenderer::GetSpriteFont(L"Consolas"), false));

    text->transform->position = Vector3(-1.0f, -0.79f, 0);
    text->transform->scale = 0.35f * Vector3::One;

    // Initialize constant buffer data
    computeShaderConstantBufferData.fovAngleY = settings->fovAngleY;
    octreeRendererConstantBufferData.fovAngleY = settings->fovAngleY;
    octreeRendererConstantBufferData.splatSize = 0.01f;
}

void OctreeRenderer::Initialize(SceneObject *sceneObject)
{
    // Create the constant buffer for WVP
    D3D11_BUFFER_DESC cbDescWVP;
    ZeroMemory(&cbDescWVP, sizeof(cbDescWVP));
    cbDescWVP.Usage = D3D11_USAGE_DEFAULT;
    cbDescWVP.ByteWidth = sizeof(OctreeRendererConstantBuffer);
    cbDescWVP.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = d3d11Device->CreateBuffer(&cbDescWVP, NULL, &octreeRendererConstantBuffer);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateBuffer) + L" failed for the " + NAMEOF(octreeRendererConstantBuffer));

    // Create the constant buffer for the compute shader
    D3D11_BUFFER_DESC computeShaderConstantBufferDesc;
    ZeroMemory(&computeShaderConstantBufferDesc, sizeof(computeShaderConstantBufferDesc));
    computeShaderConstantBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    computeShaderConstantBufferDesc.ByteWidth = sizeof(ComputeShaderConstantBuffer);
    computeShaderConstantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = d3d11Device->CreateBuffer(&computeShaderConstantBufferDesc, NULL, &computeShaderConstantBuffer);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateBuffer) + L" failed for the " + NAMEOF(computeShaderConstantBuffer));

    // Create the buffer for the compute shader that stores all the octree nodes
    // Maximum size is ~4.2 GB due to UINT_MAX
    D3D11_BUFFER_DESC nodesBufferDesc;
    ZeroMemory(&nodesBufferDesc, sizeof(nodesBufferDesc));
    nodesBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    nodesBufferDesc.ByteWidth = octree->nodes.size() * sizeof(OctreeNode);
    nodesBufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    nodesBufferDesc.StructureByteStride = sizeof(OctreeNode);
    nodesBufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    D3D11_SUBRESOURCE_DATA nodesBufferData;
    ZeroMemory(&nodesBufferData, sizeof(nodesBufferData));
	nodesBufferData.pSysMem = octree->nodes.data();

    hr = d3d11Device->CreateBuffer(&nodesBufferDesc, &nodesBufferData, &nodesBuffer);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateBuffer) + L" failed for the " + NAMEOF(nodesBuffer));

    D3D11_SHADER_RESOURCE_VIEW_DESC nodesBufferSRVDesc;
    ZeroMemory(&nodesBufferSRVDesc, sizeof(nodesBufferSRVDesc));
    nodesBufferSRVDesc.Format = DXGI_FORMAT_UNKNOWN;
    nodesBufferSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    nodesBufferSRVDesc.Buffer.ElementWidth = sizeof(OctreeNode);
    nodesBufferSRVDesc.Buffer.NumElements = octree->nodes.size();

    hr = d3d11Device->CreateShaderResourceView(nodesBuffer, &nodesBufferSRVDesc, &nodesBufferSRV);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateShaderResourceView) + L" failed for the " + NAMEOF(nodesBufferSRV));

    // Create general buffer description for append/consume buffer
    D3D11_BUFFER_DESC appendConsumeBufferDesc;
    ZeroMemory(&appendConsumeBufferDesc, sizeof(appendConsumeBufferDesc));
    appendConsumeBufferDesc.ByteWidth = maxVertexBufferCount * sizeof(UINT);
    appendConsumeBufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    appendConsumeBufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    appendConsumeBufferDesc.StructureByteStride = sizeof(UINT);
    appendConsumeBufferDesc.Usage = D3D11_USAGE_DEFAULT;

    // Create general UAV description for append/consume buffers
    D3D11_UNORDERED_ACCESS_VIEW_DESC appendConsumeBufferUAVDesc;
    ZeroMemory(&appendConsumeBufferUAVDesc, sizeof(appendConsumeBufferUAVDesc));
    appendConsumeBufferUAVDesc.Format = DXGI_FORMAT_UNKNOWN;
    appendConsumeBufferUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    appendConsumeBufferUAVDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
    appendConsumeBufferUAVDesc.Buffer.NumElements = maxVertexBufferCount;

    // Create the first buffer and its UAV
    hr = d3d11Device->CreateBuffer(&appendConsumeBufferDesc, NULL, &firstBuffer);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateBuffer) + L" failed for the " + NAMEOF(firstBuffer));

    hr = d3d11Device->CreateUnorderedAccessView(firstBuffer, &appendConsumeBufferUAVDesc, &firstBufferUAV);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateUnorderedAccessView) + L" failed for the " + NAMEOF(firstBufferUAV));

    // Create the second buffer and its UAV
    hr = d3d11Device->CreateBuffer(&appendConsumeBufferDesc, NULL, &secondBuffer);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateBuffer) + L" failed for the " + NAMEOF(secondBuffer));

    hr = d3d11Device->CreateUnorderedAccessView(secondBuffer, &appendConsumeBufferUAVDesc, &secondBufferUAV);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateUnorderedAccessView) + L" failed for the " + NAMEOF(secondBufferUAV));

    // Create the vertex append buffer
    hr = d3d11Device->CreateBuffer(&appendConsumeBufferDesc, NULL, &vertexAppendBuffer);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateBuffer) + L" failed for the " + NAMEOF(vertexAppendBuffer));

    hr = d3d11Device->CreateUnorderedAccessView(vertexAppendBuffer, &appendConsumeBufferUAVDesc, &vertexAppendBufferUAV);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateUnorderedAccessView) + L" failed for the " + NAMEOF(vertexAppendBufferUAV));

    D3D11_SHADER_RESOURCE_VIEW_DESC vertexAppendBufferSRVDesc;
    ZeroMemory(&vertexAppendBufferSRVDesc, sizeof(vertexAppendBufferSRVDesc));
    vertexAppendBufferSRVDesc.Format = DXGI_FORMAT_UNKNOWN;
    vertexAppendBufferSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    vertexAppendBufferSRVDesc.Buffer.ElementWidth = sizeof(UINT);
    vertexAppendBufferSRVDesc.Buffer.NumElements = maxVertexBufferCount;

    hr = d3d11Device->CreateShaderResourceView(vertexAppendBuffer, &vertexAppendBufferSRVDesc, &vertexAppendBufferSRV);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateShaderResourceView) + L" failed for the " + NAMEOF(vertexAppendBufferSRV));

    // Create the structure count buffer that is simply used to check for the size of the append/consume buffers
    D3D11_BUFFER_DESC structureCountBufferDesc;
    ZeroMemory(&structureCountBufferDesc, sizeof(structureCountBufferDesc));
    structureCountBufferDesc.ByteWidth = sizeof(UINT);
    structureCountBufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    structureCountBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    structureCountBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    hr = d3d11Device->CreateBuffer(&structureCountBufferDesc, NULL, &structureCountBuffer);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateBuffer) + L" failed for the " + NAMEOF(structureCountBuffer));
}

void OctreeRenderer::Update(SceneObject *sceneObject)
{
    // Select octree level with arrow keys (level -1 means that the level will be ignored)
    if (Input::GetKeyDown(Keyboard::Left) && (level > -1))
    {
        level--;
    }
    else if (Input::GetKeyDown(Keyboard::Right) && ((vertexBufferCount > 0) || (level < 0)))
    {
        level++;
    }

    // Toggle draw splats
    if (Input::GetKeyDown(Keyboard::Enter))
    {
        viewMode = (viewMode + 1) % 3;
    }

    if (Input::GetKeyDown(Keyboard::Back))
    {
        useComputeShader = !useComputeShader;
    }

    // Set the text
    textRenderer->text = useComputeShader ? L"GPU Computation\n" : L"CPU Computation\n";

    int splatSizePixels = settings->resolutionY * octreeRendererConstantBufferData.splatSize * octreeRendererConstantBufferData.overlapFactor;
    textRenderer->text.append(L"Splat Size: " + std::to_wstring(splatSizePixels) + L" Pixel\n");

    if (viewMode == 0)
    {
        textRenderer->text.append(L"Node View Mode: Splats\n");
    }
    else if (viewMode == 1)
    {
        textRenderer->text.append(L"Node View Mode: Bounding Cubes\n");
    }
    else if (viewMode == 2)
    {
        textRenderer->text.append(L"Node View Mode: Normal Clusters\n");
    }

    textRenderer->text.append(L"Octree Level: ");
    textRenderer->text.append((level < 0) ? L"AUTO" : std::to_wstring(level));
    textRenderer->text.append(L", Vertex Count: " + std::to_wstring(vertexBufferCount));
}

void OctreeRenderer::Draw(SceneObject *sceneObject)
{
    // Transform the camera position into local space and save it in the constant buffers
    Matrix world = sceneObject->transform->worldMatrix;
    Matrix worldInverse = world.Invert();
    Vector3 cameraPosition = camera->GetPosition();
    Vector3 localCameraPosition = Vector4::Transform(Vector4(cameraPosition.x, cameraPosition.y, cameraPosition.z, 1), worldInverse);

    // Set shader constant buffer variables
    octreeRendererConstantBufferData.World = world.Transpose();
    octreeRendererConstantBufferData.WorldInverseTranspose = worldInverse;
    octreeRendererConstantBufferData.View = camera->GetViewMatrix().Transpose();
    octreeRendererConstantBufferData.Projection = camera->GetProjectionMatrix().Transpose();
    octreeRendererConstantBufferData.cameraPosition = cameraPosition;

    // Draw overlapping splats to make sure that continuous surfaces are drawn
    // Higher overlap factor reduces the spacing between tilted splats but reduces the detail (blend overlapping splats to improve this)
    // 1.0f = Orthogonal splats to the camera are as large as the pixel area they should fill and do not overlap
    // 2.0f = Orthogonal splats to the camera are twice as large and overlap with all their surrounding splats
    octreeRendererConstantBufferData.overlapFactor = 1.75f;

    // Update the hlsl file buffer, set shader buffer to our created buffer
    d3d11DevCon->UpdateSubresource(octreeRendererConstantBuffer, 0, NULL, &octreeRendererConstantBufferData, 0, 0);

    // Set shader buffer
    d3d11DevCon->VSSetConstantBuffers(0, 1, &octreeRendererConstantBuffer);
    d3d11DevCon->GSSetConstantBuffers(0, 1, &octreeRendererConstantBuffer);

    // Get the vertex buffer and use the specified implementation
    if (useComputeShader)
    {
        DrawOctreeCompute(sceneObject, localCameraPosition);
    }
    else
    {
        DrawOctree(sceneObject, localCameraPosition);
    }
}

void OctreeRenderer::Release()
{
    SafeDelete(octree);

    Hierarchy::ReleaseSceneObject(text);

    SAFE_RELEASE(nodesBuffer);
    SAFE_RELEASE(firstBuffer);
    SAFE_RELEASE(secondBuffer);
    SAFE_RELEASE(vertexAppendBuffer);
    SAFE_RELEASE(structureCountBuffer);
    SAFE_RELEASE(nodesBufferSRV);
    SAFE_RELEASE(firstBufferUAV);
    SAFE_RELEASE(secondBufferUAV);
    SAFE_RELEASE(vertexAppendBufferUAV);
    SAFE_RELEASE(octreeRendererConstantBuffer);
    SAFE_RELEASE(computeShaderConstantBuffer);
}

void PointCloudEngine::OctreeRenderer::SetSplatSize(const float &splatSize)
{
    octreeRendererConstantBufferData.splatSize = splatSize;
    computeShaderConstantBufferData.splatSize = splatSize;
}

void PointCloudEngine::OctreeRenderer::GetBoundingCubePositionAndSize(Vector3 &outPosition, float &outSize)
{
    octree->GetRootPositionAndSize(outPosition, outSize);
}

void PointCloudEngine::OctreeRenderer::DrawOctree(SceneObject *sceneObject, const Vector3 &localCameraPosition)
{
    std::vector<OctreeNodeVertex> octreeVertices;

    // Create new buffer from the current octree traversal on the cpu
    if (level < 0)
    {
        octreeVertices = octree->GetVertices(localCameraPosition, octreeRendererConstantBufferData.splatSize);
    }
    else
    {
        octreeVertices = octree->GetVerticesAtLevel(level);
    }

    vertexBufferCount = octreeVertices.size();

    if (vertexBufferCount > 0)
    {
        ID3D11Buffer* vertexBuffer = NULL;

        // Create a vertex buffer description
        D3D11_BUFFER_DESC vertexBufferDesc;
        ZeroMemory(&vertexBufferDesc, sizeof(vertexBufferDesc));
        vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
        vertexBufferDesc.ByteWidth = sizeof(OctreeNodeVertex) * vertexBufferCount;
        vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertexBufferDesc.CPUAccessFlags = 0;

        // Fill a D3D11_SUBRESOURCE_DATA struct with the data we want in the buffer
        D3D11_SUBRESOURCE_DATA vertexBufferData;
        ZeroMemory(&vertexBufferData, sizeof(vertexBufferData));
        vertexBufferData.pSysMem = &octreeVertices[0];

        // Create the buffer
        hr = d3d11Device->CreateBuffer(&vertexBufferDesc, &vertexBufferData, &vertexBuffer);
		ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateBuffer) + L" failed for the " + NAMEOF(vertexBuffer));

        // Set the shaders
        if (viewMode == 0)
        {
            d3d11DevCon->VSSetShader(octreeSplatShader->vertexShader, 0, 0);
            d3d11DevCon->GSSetShader(octreeSplatShader->geometryShader, 0, 0);
            d3d11DevCon->PSSetShader(octreeSplatShader->pixelShader, 0, 0);
        }
        else if (viewMode == 1)
        {
            d3d11DevCon->VSSetShader(octreeCubeShader->vertexShader, 0, 0);
            d3d11DevCon->GSSetShader(octreeCubeShader->geometryShader, 0, 0);
            d3d11DevCon->PSSetShader(octreeCubeShader->pixelShader, 0, 0);
        }
        else if (viewMode == 2)
        {
            d3d11DevCon->VSSetShader(octreeClusterShader->vertexShader, 0, 0);
            d3d11DevCon->GSSetShader(octreeClusterShader->geometryShader, 0, 0);
            d3d11DevCon->PSSetShader(octreeClusterShader->pixelShader, 0, 0);
        }

        // Set the Input (Vertex) Layout
        d3d11DevCon->IASetInputLayout(octreeCubeShader->inputLayout);

        // Bind the vertex buffer and to the input assembler (IA)
        UINT offset = 0;
        UINT stride = sizeof(OctreeNodeVertex);
        d3d11DevCon->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

        // Set primitive topology
        d3d11DevCon->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);

        d3d11DevCon->Draw(vertexBufferCount, 0);

        SAFE_RELEASE(vertexBuffer);
    }
}

void PointCloudEngine::OctreeRenderer::DrawOctreeCompute(SceneObject *sceneObject, const Vector3 &localCameraPosition)
{
    // Set constant buffer data
    computeShaderConstantBufferData.localCameraPosition = localCameraPosition;

    // Update constant buffer
    d3d11DevCon->UpdateSubresource(computeShaderConstantBuffer, 0, NULL, &computeShaderConstantBufferData, 0, 0);

    // Set the constant buffer
    d3d11DevCon->CSSetConstantBuffers(0, 1, &computeShaderConstantBuffer);

    // This is used to unbind buffers and views from the shaders
    ID3D11Buffer* nullBuffer[1] = { NULL };
    ID3D11UnorderedAccessView* nullUAV[1] = { NULL };
    ID3D11ShaderResourceView* nullSRV[1] = { NULL };

    // Use compute shader to traverse the octree
    UINT zero = 0;
    d3d11DevCon->CSSetShader(octreeComputeShader->computeShader, 0, 0);
    d3d11DevCon->CSSetShaderResources(0, 1, &nodesBufferSRV);
    d3d11DevCon->CSSetUnorderedAccessViews(2, 1, &vertexAppendBufferUAV, &zero);

	// Set 0 as the only entry (root index) for the first buffer, will be used as input consume buffer in the shader
	d3d11DevCon->ClearUnorderedAccessViewUint(firstBufferUAV, &zero);

    // Stop iterating when all levels of the octree were checked
    UINT iteration = 0;
    UINT inputCount = 1;
    bool firstBufferIsInputConsumeBuffer = true;

    // Swap the input and output buffer as long as there is data in the output append buffer
    do
    {
        // Unbind first
        d3d11DevCon->CSSetUnorderedAccessViews(0, 1, nullUAV, &zero);
        d3d11DevCon->CSSetUnorderedAccessViews(1, 1, nullUAV, &zero);

        if (firstBufferIsInputConsumeBuffer)
        {
            d3d11DevCon->CSSetUnorderedAccessViews(0, 1, &firstBufferUAV, &inputCount);
            d3d11DevCon->CSSetUnorderedAccessViews(1, 1, &secondBufferUAV, &zero);
        }
        else
        {
            d3d11DevCon->CSSetUnorderedAccessViews(0, 1, &secondBufferUAV, &inputCount);
            d3d11DevCon->CSSetUnorderedAccessViews(1, 1, &firstBufferUAV, &zero);
        }

        // Update constant buffer to make sure that not too much is appended or consumed
        computeShaderConstantBufferData.inputCount = inputCount;
        d3d11DevCon->UpdateSubresource(computeShaderConstantBuffer, 0, NULL, &computeShaderConstantBufferData, 0, 0);

        // Execution of the compute shader, appends the indices of the nodes that should be checked next to the output append buffer
        d3d11DevCon->Dispatch(ceil(inputCount / 1024.0f), 1, 1);

        // Get the output append buffer structure count
        if (firstBufferIsInputConsumeBuffer)
        {
            inputCount = GetStructureCount(secondBufferUAV);
        }
        else
        {
            inputCount = GetStructureCount(firstBufferUAV);
        }

        // Swap the buffers
        firstBufferIsInputConsumeBuffer = !firstBufferIsInputConsumeBuffer;

    } while ((inputCount > 0) && (iteration++ < settings->maxOctreeDepth));

    // Get the actual vertex buffer count from the vertex append buffer structure counter
    vertexBufferCount = GetStructureCount(vertexAppendBufferUAV);

    // Unbind nodes and vertex append buffer in order to use it in the vertex shader
    d3d11DevCon->CSSetShaderResources(0, 1, nullSRV);
    d3d11DevCon->CSSetUnorderedAccessViews(0, 1, nullUAV, &zero);
    d3d11DevCon->CSSetUnorderedAccessViews(1, 1, nullUAV, &zero);
    d3d11DevCon->CSSetUnorderedAccessViews(2, 1, nullUAV, &zero);

    // Set the shaders, only the vertex shader is different from the CPU implementation
    d3d11DevCon->VSSetShader(octreeComputeVSShader->vertexShader, 0, 0);

    if (viewMode == 0)
    {
        d3d11DevCon->GSSetShader(octreeSplatShader->geometryShader, 0, 0);
        d3d11DevCon->PSSetShader(octreeSplatShader->pixelShader, 0, 0);
    }
    else if (viewMode == 1)
    {
        d3d11DevCon->GSSetShader(octreeCubeShader->geometryShader, 0, 0);
        d3d11DevCon->PSSetShader(octreeCubeShader->pixelShader, 0, 0);
    }
    else if (viewMode == 2)
    {
        d3d11DevCon->GSSetShader(octreeClusterShader->geometryShader, 0, 0);
        d3d11DevCon->PSSetShader(octreeClusterShader->pixelShader, 0, 0);
    }

    // Set the vertex append buffer as structured buffer in the vertex shader
    d3d11DevCon->VSSetShaderResources(0, 1, &nodesBufferSRV);
    d3d11DevCon->VSSetShaderResources(1, 1, &vertexAppendBufferSRV);

    // Set an empty input layout and vertex buffer that only sends the vertex id to the shader
    d3d11DevCon->IASetInputLayout(NULL);
    d3d11DevCon->IASetVertexBuffers(0, 1, nullBuffer, &zero, &zero);
    d3d11DevCon->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);

    d3d11DevCon->Draw(vertexBufferCount, 0);

    // Unbind the vertex shader buffers
    d3d11DevCon->VSSetShaderResources(0, 1, nullSRV);
    d3d11DevCon->VSSetShaderResources(1, 1, nullSRV);
}

UINT PointCloudEngine::OctreeRenderer::GetStructureCount(ID3D11UnorderedAccessView *UAV)
{
    int output = 0;

    if (structureCountBuffer != NULL)
    {
        // Copy the count into the buffer
        d3d11DevCon->CopyStructureCount(structureCountBuffer, 0, UAV);

        // Read the value by mapping the memory to the CPU
        D3D11_MAPPED_SUBRESOURCE mappedSubresource;
        hr = d3d11DevCon->Map(structureCountBuffer, 0, D3D11_MAP_READ, 0, &mappedSubresource);
		ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11DevCon->Map) + L" failed for the " + NAMEOF(structureCountBuffer));

        output = *(UINT*)mappedSubresource.pData;

        d3d11DevCon->Unmap(structureCountBuffer, 0);
    }

    return output;
}
