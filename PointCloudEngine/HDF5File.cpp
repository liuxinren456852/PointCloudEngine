#include "HDF5File.h"

HDF5File::HDF5File(std::wstring filename)
{
	file = new H5::H5File(std::string(filename.begin(), filename.end()).c_str(), H5F_ACC_TRUNC);
}

HDF5File::HDF5File(std::string filename)
{
	file = new H5::H5File(filename.c_str(), H5F_ACC_TRUNC);
}

HDF5File::~HDF5File()
{
	delete file;
}

H5::Group HDF5File::CreateGroup(std::wstring name)
{
	return file->createGroup(std::string(name.begin(), name.end()));
}

H5::Group HDF5File::CreateGroup(std::string name)
{
	return file->createGroup(name);
}

void HDF5File::AddColorTextureDataset(H5::Group group, std::wstring name, ID3D11Texture2D* texture, float gammaCorrection)
{
	AddColorTextureDataset(group, std::string(name.begin(), name.end()), texture, gammaCorrection);
}

void HDF5File::AddColorTextureDataset(H5::Group group, std::string name, ID3D11Texture2D* texture, float gammaCorrection)
{
	// 1. Convert the input RGBA texture into a 32bit RGBA texture
	// 2. Make it readable by the CPU and convert only the RGB content to a 8bit buffer (skip alpha)
	// 3. Use the HDF5 high level API to create an image dataset from this buffer
	ID3D11Texture2D* inputTexture = NULL;
	ID3D11Texture2D* outputTexture = NULL;
	ID3D11Texture2D* readableTexture = NULL;
	ID3D11RenderTargetView* outputTextureRTV = NULL;
	ID3D11ShaderResourceView* inputTextureSRV = NULL;

	// Get the texture description
	D3D11_TEXTURE2D_DESC inputTextureDesc;
	texture->GetDesc(&inputTextureDesc);

	// Change the bind flag to make it possible to access the texture in a shader
	inputTextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	// Create the input texture
	hr = d3d11Device->CreateTexture2D(&inputTextureDesc, NULL, &inputTexture);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateTexture2D) + L" failed!");

	// Create a shader resource view for the input texture
	D3D11_SHADER_RESOURCE_VIEW_DESC inputTextureSRVDesc;
	ZeroMemory(&inputTextureSRVDesc, sizeof(inputTextureSRVDesc));
	inputTextureSRVDesc.Format = inputTextureDesc.Format;
	inputTextureSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	inputTextureSRVDesc.Texture2D.MipLevels = 1;

	hr = d3d11Device->CreateShaderResourceView(inputTexture, &inputTextureSRVDesc, &inputTextureSRV);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateShaderResourceView) + L" failed for the " + NAMEOF(inputTexture));

	// Copy the content to the input texture
	d3d11DevCon->CopyResource(inputTexture, texture);

	// Change the output description to unsigned 32bit RGBA format and render target
	D3D11_TEXTURE2D_DESC outputTextureDesc = inputTextureDesc;
	outputTextureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	outputTextureDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
	outputTextureDesc.Usage = D3D11_USAGE_DEFAULT;
	outputTextureDesc.CPUAccessFlags = 0;

	// Create a temporary texure with that format
	hr = d3d11Device->CreateTexture2D(&outputTextureDesc, NULL, &outputTexture);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateTexture2D) + L" failed!");

	// Create render target view for this texture
	hr = d3d11Device->CreateRenderTargetView(outputTexture, NULL, &outputTextureRTV);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateRenderTargetView) + L" failed!");

	// Set the shader and resources that will be used for the texture conversion
	d3d11DevCon->VSSetShader(textureConversionShader->vertexShader, NULL, 0);
	d3d11DevCon->GSSetShader(textureConversionShader->geometryShader, NULL, 0);
	d3d11DevCon->PSSetShader(textureConversionShader->pixelShader, NULL, 0);
	d3d11DevCon->PSSetShaderResources(0, 1, &inputTextureSRV);
	d3d11DevCon->OMSetRenderTargets(1, &outputTextureRTV, NULL);

	// Perform texture conversion
	d3d11DevCon->Draw(1, 0);

	// Reset shaders, resources and render target
	d3d11DevCon->VSSetShader(NULL, NULL, 0);
	d3d11DevCon->GSSetShader(NULL, NULL, 0);
	d3d11DevCon->PSSetShader(NULL, NULL, 0);
	d3d11DevCon->PSSetShaderResources(0, 1, nullSRV);
	d3d11DevCon->OMSetRenderTargets(1, &renderTargetView, depthStencilView);

	// Change the output description to unsigned 8bit RGBA format, render target and CPU readable
	D3D11_TEXTURE2D_DESC readableTextureDesc = outputTextureDesc;
	readableTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	readableTextureDesc.Usage = D3D11_USAGE_STAGING;
	readableTextureDesc.BindFlags = 0;

	// Create a temporary CPU readable texure with that format
	hr = d3d11Device->CreateTexture2D(&readableTextureDesc, NULL, &readableTexture);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateTexture2D) + L" failed!");

	// Copy the data from the output texture to the readable texture
	d3d11DevCon->CopyResource(readableTexture, outputTexture);

	// Read the raw texture data
	D3D11_MAPPED_SUBRESOURCE subresource;
	d3d11DevCon->Map(readableTexture, 0, D3D11_MAP_READ, 0, &subresource);

	// Create a vector for the 8bit RGB data
	std::vector<BYTE> buffer;

	// Convert from 32bit float to 8bit and ignore alpha component
	for (UINT i = 0; i < subresource.DepthPitch / 4; i++)
	{
		if ((i + 1) % 4 != 0)
		{
			float f = ((float*)subresource.pData)[i];

			// Make sure that the value is in the desired range
			// It can be slightly out of range for floating point render targets
			f = max(min(1.0f, f), 0);

			// Perform gamma correction and add to buffer
			buffer.push_back(std::pow(f, gammaCorrection) * 255);
		}
	}

	// Unmap the texture
	d3d11DevCon->Unmap(readableTexture, 0);

	// Release the resources
	SAFE_RELEASE(inputTexture);
	SAFE_RELEASE(outputTexture);
	SAFE_RELEASE(readableTexture);
	SAFE_RELEASE(outputTextureRTV);
	SAFE_RELEASE(inputTextureSRV);

	// Save in a custom 8bit 3D array (height * width * depth)
	H5::DataSpace dataSpace = CreateDataspace({ readableTextureDesc.Height, readableTextureDesc.Width, 3 });

	// Create a property list to set up the chunking and ZLIB deflate compression
	H5::DSetCreatPropList propList = CreateDeflateCompressionPropList({ 64, 64, 3 });

	// Create the dataset
	H5::DataSet dataSet = group.createDataSet(name.c_str(), H5::PredType::STD_U8BE, dataSpace, propList);

	// Create attributes so that this data is interpreted as an image
	SetImageAttributes(dataSet);

	// Write the data
	dataSet.write(buffer.data(), H5::PredType::STD_U8BE);
}

void HDF5File::AddDepthTextureDataset(H5::Group group, std::wstring name, ID3D11Texture2D* texture)
{
	AddDepthTextureDataset(group, std::string(name.begin(), name.end()), texture);
}

void HDF5File::AddDepthTextureDataset(H5::Group group, std::string name, ID3D11Texture2D* texture)
{
	ID3D11Texture2D* readableTexture = NULL;

	// Get the texture description
	D3D11_TEXTURE2D_DESC textureDesc;
	texture->GetDesc(&textureDesc);

	if (textureDesc.Format != DXGI_FORMAT_R32_TYPELESS)
	{
		ERROR_MESSAGE(NAMEOF(AddDepthTextureDataset) + L" only supports textures with DXGI_FORMAT_R32_TYPELESS format!");
		return;
	}

	// Change the description to make the texture CPU readable
	textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	textureDesc.Usage = D3D11_USAGE_STAGING;
	textureDesc.BindFlags = 0;

	// Create a readable temporary texture
	hr = d3d11Device->CreateTexture2D(&textureDesc, NULL, &readableTexture);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateTexture2D) + L" failed!");

	// Copy the content of the original texture
	d3d11DevCon->CopyResource(readableTexture, texture);

	// Read the raw texture data
	D3D11_MAPPED_SUBRESOURCE subresource;
	d3d11DevCon->Map(readableTexture, 0, D3D11_MAP_READ, 0, &subresource);

	// Save in a HDF5 2D array
	H5::DataSpace dataSpace = CreateDataspace({ textureDesc.Height, textureDesc.Width });

	// Create a property list to set up the chunking and ZLIB deflate compression
	H5::DSetCreatPropList propList = CreateDeflateCompressionPropList({ 64, 64 });

	// Create the dataset
	H5::DataSet dataSet = group.createDataSet(name.c_str(), H5::PredType::NATIVE_FLOAT, dataSpace, propList);

	// Create attributes so that this data is interpreted as an image
	SetImageAttributes(dataSet);

	// Write the data
	dataSet.write(subresource.pData, H5::PredType::NATIVE_FLOAT);
	
	// Unmap the texture
	d3d11DevCon->Unmap(readableTexture, 0);

	SAFE_RELEASE(readableTexture);
}

H5::DataSpace HDF5File::CreateDataspace(std::initializer_list<hsize_t> dimensions)
{
	return H5::DataSpace(dimensions.size(), dimensions.begin());
}

H5::DSetCreatPropList HDF5File::CreateDeflateCompressionPropList(std::initializer_list<hsize_t> chunkDimensions, int deflateLevel)
{
	H5::DSetCreatPropList propList;
	propList.setChunk(chunkDimensions.size(), chunkDimensions.begin());
	propList.setDeflate(deflateLevel);

	return propList;
}

void HDF5File::SetImageAttributes(H5::DataSet dataSet)
{
	H5::DataSpace attributeDataspace(H5S_SCALAR);
	H5::StrType classType(H5::PredType::C_S1, 5);
	H5::Attribute classAttribute = dataSet.createAttribute("CLASS", classType, attributeDataspace);
	classAttribute.write(classType, std::string("IMAGE"));

	H5::StrType versionType(H5::PredType::C_S1, 3);
	H5::Attribute versionAttribute = dataSet.createAttribute("IMAGE_VERSION", versionType, attributeDataspace);
	versionAttribute.write(versionType, std::string("1.2"));

	H5::StrType subclassType(H5::PredType::C_S1, 15);
	H5::Attribute subclassAttribute = dataSet.createAttribute("IMAGE_SUBCLASS", subclassType, attributeDataspace);
	subclassAttribute.write(subclassType, std::string("IMAGE_TRUECOLOR"));

	H5::StrType interlaceType(H5::PredType::C_S1, 15);
	H5::Attribute interlaceAttribute = dataSet.createAttribute("INTERLACE_MODE", interlaceType, attributeDataspace);
	interlaceAttribute.write(interlaceType, std::string("INTERLACE_PIXEL"));
}