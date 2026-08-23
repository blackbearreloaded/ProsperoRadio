#include "osmesa_render_interface.h"

#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_video.h>

#include <new>

struct OSMesaRenderInterface::GlApi {
	using ViewportFn = void(GLAPIENTRY*)(GLint, GLint, GLsizei, GLsizei);
	using EnableClientStateFn = void(GLAPIENTRY*)(GLenum);
	using DisableClientStateFn = void(GLAPIENTRY*)(GLenum);
	using EnableFn = void(GLAPIENTRY*)(GLenum);
	using DisableFn = void(GLAPIENTRY*)(GLenum);
	using BlendFuncFn = void(GLAPIENTRY*)(GLenum, GLenum);
	using MatrixModeFn = void(GLAPIENTRY*)(GLenum);
	using LoadIdentityFn = void(GLAPIENTRY*)();
	using OrthoFn = void(GLAPIENTRY*)(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble);
	using ClearColorFn = void(GLAPIENTRY*)(GLclampf, GLclampf, GLclampf, GLclampf);
	using ClearFn = void(GLAPIENTRY*)(GLbitfield);
	using PushMatrixFn = void(GLAPIENTRY*)();
	using PopMatrixFn = void(GLAPIENTRY*)();
	using TranslatefFn = void(GLAPIENTRY*)(GLfloat, GLfloat, GLfloat);
	using VertexPointerFn = void(GLAPIENTRY*)(GLint, GLenum, GLsizei, const GLvoid*);
	using ColorPointerFn = void(GLAPIENTRY*)(GLint, GLenum, GLsizei, const GLvoid*);
	using TexCoordPointerFn = void(GLAPIENTRY*)(GLint, GLenum, GLsizei, const GLvoid*);
	using BindTextureFn = void(GLAPIENTRY*)(GLenum, GLuint);
	using DrawElementsFn = void(GLAPIENTRY*)(GLenum, GLsizei, GLenum, const GLvoid*);
	using ScissorFn = void(GLAPIENTRY*)(GLint, GLint, GLsizei, GLsizei);
	using GenTexturesFn = void(GLAPIENTRY*)(GLsizei, GLuint*);
	using DeleteTexturesFn = void(GLAPIENTRY*)(GLsizei, const GLuint*);
	using TexImage2DFn = void(GLAPIENTRY*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*);
	using TexParameteriFn = void(GLAPIENTRY*)(GLenum, GLenum, GLint);
	using PixelStoreiFn = void(GLAPIENTRY*)(GLenum, GLint);

	ViewportFn Viewport = nullptr;
	EnableClientStateFn EnableClientState = nullptr;
	DisableClientStateFn DisableClientState = nullptr;
	EnableFn Enable = nullptr;
	DisableFn Disable = nullptr;
	BlendFuncFn BlendFunc = nullptr;
	MatrixModeFn MatrixMode = nullptr;
	LoadIdentityFn LoadIdentity = nullptr;
	OrthoFn Ortho = nullptr;
	ClearColorFn ClearColor = nullptr;
	ClearFn Clear = nullptr;
	PushMatrixFn PushMatrix = nullptr;
	PopMatrixFn PopMatrix = nullptr;
	TranslatefFn Translatef = nullptr;
	VertexPointerFn VertexPointer = nullptr;
	ColorPointerFn ColorPointer = nullptr;
	TexCoordPointerFn TexCoordPointer = nullptr;
	BindTextureFn BindTexture = nullptr;
	DrawElementsFn DrawElements = nullptr;
	ScissorFn Scissor = nullptr;
	GenTexturesFn GenTextures = nullptr;
	DeleteTexturesFn DeleteTextures = nullptr;
	TexImage2DFn TexImage2D = nullptr;
	TexParameteriFn TexParameteri = nullptr;
	PixelStoreiFn PixelStorei = nullptr;

	bool Load()
	{
#define LOAD_GL(member)                                                                                                  \
	member = reinterpret_cast<decltype(member)>(SDL_GL_GetProcAddress("gl" #member));                                     \
	if (!member) return false
		LOAD_GL(Viewport);
		LOAD_GL(EnableClientState);
		LOAD_GL(DisableClientState);
		LOAD_GL(Enable);
		LOAD_GL(Disable);
		LOAD_GL(BlendFunc);
		LOAD_GL(MatrixMode);
		LOAD_GL(LoadIdentity);
		LOAD_GL(Ortho);
		LOAD_GL(ClearColor);
		LOAD_GL(Clear);
		LOAD_GL(PushMatrix);
		LOAD_GL(PopMatrix);
		LOAD_GL(Translatef);
		LOAD_GL(VertexPointer);
		LOAD_GL(ColorPointer);
		LOAD_GL(TexCoordPointer);
		LOAD_GL(BindTexture);
		LOAD_GL(DrawElements);
		LOAD_GL(Scissor);
		LOAD_GL(GenTextures);
		LOAD_GL(DeleteTextures);
		LOAD_GL(TexImage2D);
		LOAD_GL(TexParameteri);
		LOAD_GL(PixelStorei);
#undef LOAD_GL
		return true;
	}
};

OSMesaRenderInterface::OSMesaRenderInterface(int viewport_width, int viewport_height) :
	viewport_width_(viewport_width), viewport_height_(viewport_height)
{}

OSMesaRenderInterface::~OSMesaRenderInterface()
{
	delete gl_;
}

bool OSMesaRenderInterface::Initialize()
{
	if (gl_) return true;
	GlApi* api = new (std::nothrow) GlApi;
	if (!api || !api->Load()) {
		delete api;
		return false;
	}
	gl_ = api;
	return true;
}

void OSMesaRenderInterface::BeginFrame()
{
	if (!gl_) return;
	gl_->Viewport(0, 0, viewport_width_, viewport_height_);
	gl_->Disable(GL_DEPTH_TEST);
	gl_->Disable(GL_CULL_FACE);
	gl_->Disable(GL_SCISSOR_TEST);
	gl_->Enable(GL_BLEND);
	gl_->BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gl_->EnableClientState(GL_VERTEX_ARRAY);
	gl_->EnableClientState(GL_COLOR_ARRAY);
	gl_->DisableClientState(GL_TEXTURE_COORD_ARRAY);

	gl_->MatrixMode(GL_PROJECTION);
	gl_->LoadIdentity();
	gl_->Ortho(0.0, viewport_width_, viewport_height_, 0.0, -1.0, 1.0);
	gl_->MatrixMode(GL_TEXTURE);
	gl_->LoadIdentity();
	gl_->MatrixMode(GL_MODELVIEW);
	gl_->LoadIdentity();
}

void OSMesaRenderInterface::Clear(float red, float green, float blue, float alpha)
{
	if (!gl_) return;
	gl_->Disable(GL_SCISSOR_TEST);
	gl_->ClearColor(red, green, blue, alpha);
	gl_->Clear(GL_COLOR_BUFFER_BIT);
}

void OSMesaRenderInterface::RenderGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices,
	Rml::TextureHandle texture, const Rml::Vector2f& translation)
{
	if (!gl_ || !vertices || num_vertices <= 0 || !indices || num_indices <= 0) return;

	gl_->PushMatrix();
	gl_->Translatef(translation.x, translation.y, 0.0f);
	gl_->VertexPointer(2, GL_FLOAT, sizeof(Rml::Vertex), &vertices[0].position);
	gl_->ColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Rml::Vertex), &vertices[0].colour);

	if (texture) {
		gl_->Enable(GL_TEXTURE_2D);
		gl_->BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
		gl_->EnableClientState(GL_TEXTURE_COORD_ARRAY);
		gl_->TexCoordPointer(2, GL_FLOAT, sizeof(Rml::Vertex), &vertices[0].tex_coord);
	} else {
		gl_->Disable(GL_TEXTURE_2D);
		gl_->DisableClientState(GL_TEXTURE_COORD_ARRAY);
	}

	gl_->DrawElements(GL_TRIANGLES, num_indices, GL_UNSIGNED_INT, indices);
	gl_->PopMatrix();
}

void OSMesaRenderInterface::EnableScissorRegion(bool enable)
{
	if (!gl_) return;
	if (enable)
		gl_->Enable(GL_SCISSOR_TEST);
	else
		gl_->Disable(GL_SCISSOR_TEST);
}

void OSMesaRenderInterface::SetScissorRegion(int x, int y, int width, int height)
{
	if (!gl_) return;
	gl_->Scissor(x, viewport_height_ - (y + height), width, height);
}

bool OSMesaRenderInterface::GenerateTexture(Rml::TextureHandle& texture_handle, const Rml::byte* source,
	const Rml::Vector2i& source_dimensions)
{
	texture_handle = {};
	if (!gl_ || !source || source_dimensions.x <= 0 || source_dimensions.y <= 0) return false;

	GLuint texture = 0;
	gl_->GenTextures(1, &texture);
	if (!texture) return false;

	gl_->BindTexture(GL_TEXTURE_2D, texture);
	gl_->PixelStorei(GL_UNPACK_ALIGNMENT, 1);
	gl_->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, source_dimensions.x, source_dimensions.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, source);
	gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	texture_handle = static_cast<Rml::TextureHandle>(texture);
	return true;
}

void OSMesaRenderInterface::ReleaseTexture(Rml::TextureHandle texture)
{
	if (!gl_ || !texture) return;
	const GLuint texture_id = static_cast<GLuint>(texture);
	gl_->DeleteTextures(1, &texture_id);
}
