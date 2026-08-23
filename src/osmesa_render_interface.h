#pragma once

#include <RmlUi/Core/RenderInterfaceCompatibility.h>

class OSMesaRenderInterface final : public Rml::RenderInterfaceCompatibility {
public:
	explicit OSMesaRenderInterface(int viewport_width = 1920, int viewport_height = 1080);
	~OSMesaRenderInterface() override;

	// Call after the OSMesa/OpenGL context is current.
	bool Initialize();
	void BeginFrame();
	void Clear(float red, float green, float blue, float alpha = 1.0f);

	void RenderGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices,
		Rml::TextureHandle texture, const Rml::Vector2f& translation) override;
	void EnableScissorRegion(bool enable) override;
	void SetScissorRegion(int x, int y, int width, int height) override;
	bool GenerateTexture(Rml::TextureHandle& texture_handle, const Rml::byte* source,
		const Rml::Vector2i& source_dimensions) override;
	void ReleaseTexture(Rml::TextureHandle texture) override;

private:
	struct GlApi;
	GlApi* gl_ = nullptr;
	int viewport_width_;
	int viewport_height_;
};
