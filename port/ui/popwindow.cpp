#include "ui/popwindow.h"
#ifdef GE_IMGUI
#include "ge.h"
#include "gl.h"
#include "imgui_impl_sdl3.h"
#include <algorithm>
#include <climits>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace ge
{

struct Picture
{
	int w{}, h{}; //pixels
	float dw{}, dh{}; //the ImGui frame's display size
	std::vector<ImDrawVert> vtx;
	std::vector<ImDrawIdx> idx;
	struct Cmd
	{
		ImVec4 clip;
		unsigned count, idxAt, vtxAt;
	};
	std::vector<Cmd> cmds;
};

#define POP_GL_FUNCS(X) \
	X(PFNGLCREATESHADERPROC, CreateShader) X(PFNGLSHADERSOURCEPROC, ShaderSource) X(PFNGLCOMPILESHADERPROC, CompileShader) \
	X(PFNGLCREATEPROGRAMPROC, CreateProgram) X(PFNGLATTACHSHADERPROC, AttachShader) X(PFNGLLINKPROGRAMPROC, LinkProgram) \
	X(PFNGLDELETESHADERPROC, DeleteShader) X(PFNGLDELETEPROGRAMPROC, DeleteProgram) X(PFNGLUSEPROGRAMPROC, UseProgram) \
	X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) X(PFNGLUNIFORMMATRIX4FVPROC, UniformMatrix4fv) X(PFNGLUNIFORM1IPROC, Uniform1i) \
	X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays) X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray) X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays) \
	X(PFNGLGENBUFFERSPROC, GenBuffers) X(PFNGLBINDBUFFERPROC, BindBuffer) X(PFNGLBUFFERDATAPROC, BufferData) X(PFNGLDELETEBUFFERSPROC, DeleteBuffers) \
	X(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray) X(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer) \
	X(PFNGLGENTEXTURESPROC, GenTextures) X(PFNGLBINDTEXTUREPROC, BindTexture) X(PFNGLTEXPARAMETERIPROC, TexParameteri) \
	X(PFNGLTEXIMAGE2DPROC, TexImage2D) X(PFNGLDELETETEXTURESPROC, DeleteTextures) X(PFNGLACTIVETEXTUREPROC, ActiveTexture) \
	X(PFNGLVIEWPORTPROC, Viewport) X(PFNGLCLEARCOLORPROC, ClearColor) X(PFNGLCLEARPROC, Clear) X(PFNGLENABLEPROC, Enable) \
	X(PFNGLDISABLEPROC, Disable) X(PFNGLBLENDEQUATIONPROC, BlendEquation) X(PFNGLBLENDFUNCSEPARATEPROC, BlendFuncSeparate) \
	X(PFNGLSCISSORPROC, Scissor) X(PFNGLDRAWELEMENTSBASEVERTEXPROC, DrawElementsBaseVertex) X(PFNGLREADPIXELSPROC, ReadPixels) \
	X(PFNGLPIXELSTOREIPROC, PixelStorei)

class PopRenderer
{
public:
	//on the game's thread: the context is made for the window and the game's made current again
	bool Start(SDL_Window* pWindow, const unsigned char* font, int fontW, int fontH, const char* name)
	{
		m_pWindow = pWindow;
		m_Name = name;
		m_Font.assign(font, font + (size_t)fontW * fontH * 4);
		m_FontW = fontW;
		m_FontH = fontH;
		SDL_Window* game = SDL_GL_GetCurrentWindow();
		SDL_GLContext gameContext = SDL_GL_GetCurrentContext();
		m_Context = SDL_GL_CreateContext(pWindow);
		if (game && gameContext) SDL_GL_MakeCurrent(game, gameContext);
		if (!m_Context) return false;
		m_Thread = SDL_CreateThread([](void* me) { static_cast<PopRenderer*>(me)->Run(); return 0; }, m_Name.c_str(), this);
		return m_Thread != nullptr;
	}

	void Stop()
	{
		{
			std::lock_guard<std::mutex> hold(m_Lock);
			m_Quit = true;
		}
		m_Wake.notify_one();
		if (m_Thread) SDL_WaitThread(m_Thread, nullptr);
		if (m_Context) SDL_GL_DestroyContext(m_Context);
		m_Thread = nullptr;
		m_Context = nullptr;
	}

	void Show(Picture&& picture)
	{
		{
			std::lock_guard<std::mutex> hold(m_Lock);
			m_Next = std::move(picture);
			m_Have = true;
		}
		m_Wake.notify_one();
	}

private:
	struct Gl
	{
#define POP_GL_DECLARE(T, n) T n{};
		POP_GL_FUNCS(POP_GL_DECLARE)
#undef POP_GL_DECLARE
	} gl;

	void Run()
	{
		if (!SDL_GL_MakeCurrent(m_pWindow, m_Context)) return;
		SDL_GL_SetSwapInterval(0); //its presents never wait for the screen
#define POP_GL_LOAD(T, n) gl.n = (T)SDL_GL_GetProcAddress("gl" #n);
		POP_GL_FUNCS(POP_GL_LOAD)
#undef POP_GL_LOAD
		Setup();
		for (;;)
		{
			Picture now;
			{
				std::unique_lock<std::mutex> hold(m_Lock);
				m_Wake.wait(hold, [this] { return m_Quit || m_Have; });
				if (m_Quit) break;
				now = std::move(m_Next);
				m_Have = false;
			}
			Draw(now);
		}
		gl.DeleteTextures(1, &m_Texture);
		gl.DeleteBuffers(1, &m_Vbo);
		gl.DeleteBuffers(1, &m_Ibo);
		gl.DeleteVertexArrays(1, &m_Vao);
		gl.DeleteProgram(m_Program);
		SDL_GL_MakeCurrent(m_pWindow, nullptr);
	}

	void Setup()
	{
		const char* vs = "#version 330 core\n"
			"layout(location = 0) in vec2 Position; layout(location = 1) in vec2 UV; layout(location = 2) in vec4 Color;\n"
			"uniform mat4 Proj; out vec2 FragUV; out vec4 FragColor;\n"
			"void main() { FragUV = UV; FragColor = Color; gl_Position = Proj * vec4(Position, 0, 1); }\n";
		const char* fs = "#version 330 core\n"
			"in vec2 FragUV; in vec4 FragColor; uniform sampler2D Texture; out vec4 OutColor;\n"
			"void main() { OutColor = FragColor * texture(Texture, FragUV); }\n";
		GLuint v = gl.CreateShader(GL_VERTEX_SHADER), f = gl.CreateShader(GL_FRAGMENT_SHADER);
		gl.ShaderSource(v, 1, &vs, nullptr);
		gl.ShaderSource(f, 1, &fs, nullptr);
		gl.CompileShader(v);
		gl.CompileShader(f);
		m_Program = gl.CreateProgram();
		gl.AttachShader(m_Program, v);
		gl.AttachShader(m_Program, f);
		gl.LinkProgram(m_Program);
		gl.DeleteShader(v);
		gl.DeleteShader(f);
		m_Proj = gl.GetUniformLocation(m_Program, "Proj");
		m_Sampler = gl.GetUniformLocation(m_Program, "Texture");
		gl.GenVertexArrays(1, &m_Vao);
		gl.BindVertexArray(m_Vao);
		gl.GenBuffers(1, &m_Vbo);
		gl.GenBuffers(1, &m_Ibo);
		gl.BindBuffer(GL_ARRAY_BUFFER, m_Vbo);
		gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_Ibo);
		gl.EnableVertexAttribArray(0);
		gl.EnableVertexAttribArray(1);
		gl.EnableVertexAttribArray(2);
		gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (void*)offsetof(ImDrawVert, pos));
		gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (void*)offsetof(ImDrawVert, uv));
		gl.VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (void*)offsetof(ImDrawVert, col));
		gl.GenTextures(1, &m_Texture);
		gl.BindTexture(GL_TEXTURE_2D, m_Texture);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
		gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_FontW, m_FontH, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_Font.data());
	}

	void Draw(const Picture& p)
	{
		gl.Viewport(0, 0, p.w, p.h);
		gl.ClearColor(6 / 255.0f, 14 / 255.0f, 18 / 255.0f, 1);
		gl.Disable(GL_SCISSOR_TEST);
		gl.Clear(GL_COLOR_BUFFER_BIT);
		gl.Enable(GL_BLEND);
		gl.BlendEquation(GL_FUNC_ADD);
		gl.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		gl.Disable(GL_CULL_FACE);
		gl.Disable(GL_DEPTH_TEST);
		gl.Enable(GL_SCISSOR_TEST);
		float l = 0, r = p.dw > 0 ? p.dw : (float)p.w, t = 0, b = p.dh > 0 ? p.dh : (float)p.h;
		const float sx = (float)p.w / r, sy = (float)p.h / b; //points to pixels
		const float proj[16] = { 2 / (r - l), 0, 0, 0, 0, 2 / (t - b), 0, 0, 0, 0, -1, 0, (r + l) / (l - r), (t + b) / (b - t), 0, 1 };
		gl.UseProgram(m_Program);
		gl.UniformMatrix4fv(m_Proj, 1, GL_FALSE, proj);
		gl.Uniform1i(m_Sampler, 0);
		gl.ActiveTexture(GL_TEXTURE0);
		gl.BindTexture(GL_TEXTURE_2D, m_Texture);
		gl.BindVertexArray(m_Vao);
		gl.BindBuffer(GL_ARRAY_BUFFER, m_Vbo);
		gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(p.vtx.size() * sizeof(ImDrawVert)), p.vtx.data(), GL_STREAM_DRAW);
		gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(p.idx.size() * sizeof(ImDrawIdx)), p.idx.data(), GL_STREAM_DRAW);
		for (const Picture::Cmd& c : p.cmds)
		{
			if (c.clip.z <= c.clip.x || c.clip.w <= c.clip.y) continue;
			gl.Scissor((GLint)(c.clip.x * sx), (GLint)((float)p.h - c.clip.w * sy), (GLsizei)((c.clip.z - c.clip.x) * sx), (GLsizei)((c.clip.w - c.clip.y) * sy));
			gl.DrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)c.count, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
				(void*)(size_t)(c.idxAt * sizeof(ImDrawIdx)), (GLint)c.vtxAt);
		}
		//GE_SPEEDRUN_SHOT=F.bmp the timer's first picture, GE_POPWINDOW_SHOT=F.bmp the other windows' every 120th
		static bool shot = false;
		const bool timer = m_Name == "speedrun timer";
		if (const char* path = std::getenv(timer ? "GE_SPEEDRUN_SHOT" : "GE_POPWINDOW_SHOT"); path && (timer ? !shot : ++m_Pictures % 120 == 0))
		{
			std::vector<uint8_t> rgba((size_t)p.w * p.h * 4), flipped(rgba.size());
			gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
			gl.ReadPixels(0, 0, p.w, p.h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
			for (int y = 0; y < p.h; ++y) std::memcpy(&flipped[(size_t)y * p.w * 4], &rgba[(size_t)(p.h - 1 - y) * p.w * 4], (size_t)p.w * 4);
			if (SDL_Surface* s = SDL_CreateSurfaceFrom(p.w, p.h, SDL_PIXELFORMAT_ABGR8888, flipped.data(), p.w * 4))
			{
				shot = SDL_SaveBMP(s, path);
				SDL_DestroySurface(s);
			}
		}
		SDL_GL_SwapWindow(m_pWindow);
	}

	SDL_Window* m_pWindow{ nullptr };
	std::string m_Name;
	unsigned m_Pictures{ 0 };
	SDL_GLContext m_Context{ nullptr };
	SDL_Thread* m_Thread{ nullptr };
	std::mutex m_Lock;
	std::condition_variable m_Wake;
	Picture m_Next;
	bool m_Have{ false }, m_Quit{ false };
	std::vector<unsigned char> m_Font;
	int m_FontW{}, m_FontH{};
	GLuint m_Program{}, m_Vao{}, m_Vbo{}, m_Ibo{}, m_Texture{};
	GLint m_Proj{}, m_Sampler{};
};

PopWindow::PopWindow() = default;
PopWindow::~PopWindow() { (void)m_pRenderer.release(); } //at exit: SDL may be gone, nothing is torn down under it

bool PopWindow::Open(const char* title, int x, int y, int w, int h, bool input, const std::function<void(ImFontAtlas*)>& fonts)
{
	if (m_pWindow) return true;
	SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (std::getenv("WIIKIT_HIDDEN") ? SDL_WINDOW_HIDDEN : 0);
	m_pWindow = SDL_CreateWindow(title, std::max(w, 64), std::max(h, 32), flags);
	if (!m_pWindow)
	{
		rt_log("goldeneye: no window \"%s\": %s", title, SDL_GetError());
		return false;
	}
	if (x != INT_MIN && y != INT_MIN) SDL_SetWindowPosition(m_pWindow, x, y);
	window_icon(m_pWindow);
	m_Input = input;
	ImGuiContext* outer = ImGui::GetCurrentContext();
	m_pContext = ImGui::CreateContext();
	ImGui::SetCurrentContext(m_pContext);
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; //the cursor is the game window's to show or hide
	if (fonts) fonts(io.Fonts);
	else io.Fonts->AddFontDefault();
	unsigned char* pixels = nullptr;
	int fw = 0, fh = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &fw, &fh);
	io.Fonts->SetTexID((ImTextureID)1); //the thread's only texture
	if (input) ImGui_ImplSDL3_InitForOther(m_pWindow);
	m_pRenderer = std::make_unique<PopRenderer>();
	const bool started = m_pRenderer->Start(m_pWindow, pixels, fw, fh, title && !std::strcmp(title, "Speedrun Timer") ? "speedrun timer" : "pop window");
	ImGui::SetCurrentContext(outer);
	if (!started)
	{
		rt_log("goldeneye: window \"%s\" can't be drawn: %s", title, SDL_GetError());
		Close();
		return false;
	}
	return true;
}

void PopWindow::Close()
{
	if (m_pRenderer) m_pRenderer->Stop();
	m_pRenderer.reset();
	if (m_pContext)
	{
		ImGuiContext* outer = ImGui::GetCurrentContext();
		ImGui::SetCurrentContext(m_pContext);
		if (m_Input) ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext(m_pContext);
		ImGui::SetCurrentContext(outer == m_pContext ? nullptr : outer);
	}
	if (m_pWindow) SDL_DestroyWindow(m_pWindow);
	m_pContext = nullptr;
	m_pWindow = nullptr;
}

bool PopWindow::Focused() const { return m_pWindow && (SDL_GetWindowFlags(m_pWindow) & SDL_WINDOW_INPUT_FOCUS); }

void PopWindow::Begin()
{
	m_pOuter = ImGui::GetCurrentContext();
	ImGui::SetCurrentContext(m_pContext);
	if (m_Input) ImGui_ImplSDL3_NewFrame();
	else
	{
		int w = 1, h = 1;
		SDL_GetWindowSizeInPixels(m_pWindow, &w, &h);
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2((float)std::max(w, 1), (float)std::max(h, 1));
		io.DeltaTime = 1.0f / 60;
	}
	ImGui::NewFrame();
}

void PopWindow::End()
{
	ImGui::Render();
	Picture p;
	SDL_GetWindowSizeInPixels(m_pWindow, &p.w, &p.h);
	p.w = std::max(p.w, 1);
	p.h = std::max(p.h, 1);
	const ImDrawData* d = ImGui::GetDrawData();
	p.dw = d->DisplaySize.x > 0 ? d->DisplaySize.x : (float)p.w;
	p.dh = d->DisplaySize.y > 0 ? d->DisplaySize.y : (float)p.h;
	for (int n = 0; n < d->CmdListsCount; ++n)
	{
		const ImDrawList* list = d->CmdLists[n];
		unsigned vtxBase = (unsigned)p.vtx.size(), idxBase = (unsigned)p.idx.size();
		p.vtx.insert(p.vtx.end(), list->VtxBuffer.begin(), list->VtxBuffer.end());
		p.idx.insert(p.idx.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());
		for (const ImDrawCmd& c : list->CmdBuffer)
			if (!c.UserCallback) p.cmds.push_back({ c.ClipRect, c.ElemCount, idxBase + c.IdxOffset, vtxBase + c.VtxOffset });
	}
	ImGui::SetCurrentContext(m_pOuter);
	m_pRenderer->Show(std::move(p));
}

bool PopWindow::Event(const SDL_Event* e)
{
	if (!m_pWindow || event_window(e) != SDL_GetWindowID(m_pWindow)) return false;
	if (m_Input)
	{
		ImGuiContext* outer = ImGui::GetCurrentContext();
		ImGui::SetCurrentContext(m_pContext);
		ImGui_ImplSDL3_ProcessEvent(e);
		ImGui::SetCurrentContext(outer);
	}
	return true;
}

SDL_WindowID event_window(const SDL_Event* e)
{
	if (e->type >= SDL_EVENT_WINDOW_FIRST && e->type <= SDL_EVENT_WINDOW_LAST) return e->window.windowID;
	switch (e->type)
	{
	case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: return e->key.windowID;
	case SDL_EVENT_TEXT_INPUT: return e->text.windowID;
	case SDL_EVENT_MOUSE_MOTION: return e->motion.windowID;
	case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: return e->button.windowID;
	case SDL_EVENT_MOUSE_WHEEL: return e->wheel.windowID;
	default: return 0;
	}
}

} //namespace ge
#endif
