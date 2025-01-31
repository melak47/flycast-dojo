/*
	Copyright 2021 flyinghead

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "dx11_overlay.h"
#include "rend/osd.h"
#include "rend/gui.h"

void DX11Overlay::draw(u32 width, u32 height, bool vmu, bool crosshair)
{
	RECT rect { 0, 0, (LONG)width, (LONG)height };
	deviceContext->RSSetScissorRects(1, &rect);
	if (vmu)
	{
		float vmu_padding = 8.f * scaling;
		float vmu_height = 70.f * scaling;
		float vmu_width = 48.f / 32.f * vmu_height;

		const float blend_factor[4] = { 0.75f, 0.75f, 0.75f, 0.75f };
		deviceContext->OMSetBlendState(blendStates.getState(true, 8, 8), blend_factor, 0xffffffff);

		for (size_t i = 0; i < vmuTextures.size(); i++)
		{
			if (!vmu_lcd_status[i])
			{
				vmuTextureViews[i].reset();
				vmuTextures[i].reset();
				continue;
			}
			if (vmuTextures[i] == nullptr || vmu_lcd_changed[i])
			{
				vmuTextureViews[i].reset();
				vmuTextures[i].reset();
				D3D11_TEXTURE2D_DESC desc{};
				desc.Width = 48;
				desc.Height = 32;
				desc.ArraySize = 1;
				desc.SampleDesc.Count = 1;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				desc.MipLevels = 1;

				if (SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &vmuTextures[i].get())))
				{
					D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
					viewDesc.Format = desc.Format;
					viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
					viewDesc.Texture2D.MipLevels = desc.MipLevels;
					device->CreateShaderResourceView(vmuTextures[i], &viewDesc, &vmuTextureViews[i].get());

					u32 data[48 * 32];
					for (int y = 0; y < 32; y++)
						memcpy(&data[y * 48], &vmu_lcd_data[i][(31 - y) * 48], sizeof(u32) * 48);
					deviceContext->UpdateSubresource(vmuTextures[i], 0, nullptr, data, 48 * 4, 48 * 4 * 32);
				}
				vmu_lcd_changed[i] = false;
			}
			float x;
			if (i & 2)
				x = width - vmu_padding - vmu_width;
			else
				x = vmu_padding;
			float y;
			if (i & 4)
			{
				y = height - vmu_padding - vmu_height;
				if (i & 1)
					y -= vmu_padding + vmu_height;
			}
			else
			{
				y = vmu_padding;
				if (i & 1)
					y += vmu_padding + vmu_height;
			}
			D3D11_VIEWPORT vp{};
			vp.TopLeftX = x;
			vp.TopLeftY = y;
			vp.Width = vmu_width;
			vp.Height = vmu_height;
			vp.MinDepth = 0.f;
			vp.MaxDepth = 1.f;
			deviceContext->RSSetViewports(1, &vp);
			quad.draw(vmuTextureViews[i], samplers->getSampler(false));
		}
	}
	if (crosshair)
	{
		if (!xhairTexture)
		{
			const u32* texData = getCrosshairTextureData();
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = 16;
			desc.Height = 16;
			desc.ArraySize = 1;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			desc.MipLevels = 1;

			if (SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &xhairTexture.get())))
			{
				D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
				viewDesc.Format = desc.Format;
				viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				viewDesc.Texture2D.MipLevels = desc.MipLevels;
				device->CreateShaderResourceView(xhairTexture, &viewDesc, &xhairTextureView.get());

				deviceContext->UpdateSubresource(xhairTexture, 0, nullptr, texData, 16 * 4, 16 * 4 * 16);
			}
		}
		for (u32 i = 0; i < config::CrosshairColor.size(); i++)
		{
			if (config::CrosshairColor[i] == 0)
				continue;
			if (settings.platform.system == DC_PLATFORM_DREAMCAST
					&& config::MapleMainDevices[i] != MDT_LightGun)
				continue;

			float x, y;
			std::tie(x, y) = getCrosshairPosition(i);
			float halfWidth = XHAIR_WIDTH * gui_get_scaling() / 2.f;
			D3D11_VIEWPORT vp{};
			vp.TopLeftX = x - halfWidth;
			vp.TopLeftY = y - halfWidth;
			vp.Width = halfWidth * 2;
			vp.Height = halfWidth * 2;
			vp.MinDepth = 0.f;
			vp.MaxDepth = 1.f;
			deviceContext->RSSetViewports(1, &vp);

			const float colors[4] = {
					(config::CrosshairColor[i] & 0xff) / 255.f,
					((config::CrosshairColor[i] >> 8) & 0xff) / 255.f,
					((config::CrosshairColor[i] >> 16) & 0xff) / 255.f,
					((config::CrosshairColor[i] >> 24) & 0xff) / 255.f
			};
			deviceContext->OMSetBlendState(blendStates.getState(true, 4, 5), nullptr, 0xffffffff);
			quad.draw(xhairTextureView, samplers->getSampler(false), colors);
		}
	}
}
