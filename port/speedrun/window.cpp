//the splits drawn: a box in the game's window, dragged with the mouse wherever the cursor is free,
//or a window of its own (a second ImGui context, drawn by a thread with a GL context of its own)
#include "speedrun/splits.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#ifdef GE_IMGUI
#include "gl.h"
#include <SDL3/SDL.h>
#endif

namespace ge
{

using namespace speedrun;

#ifdef GE_IMGUI
namespace
{

const ImU32 kCream = IM_COL32(243, 232, 208, 255), kBlue = IM_COL32(88, 124, 170, 255),
	kBar = IM_COL32(70, 100, 146, 150), kSelected = IM_COL32(255, 255, 255, 245),
	kSelectedText = IM_COL32(84, 146, 212, 255), kPanel = IM_COL32(6, 14, 18, 205), kGrid = IM_COL32(90, 150, 150, 22),
	kDark = IM_COL32(26, 32, 40, 255), kAhead = IM_COL32(92, 214, 112, 255), kBehind = IM_COL32(232, 76, 62, 255),
	kGoldColour = IM_COL32(255, 204, 64, 255);
const float kWidth = 330, kHead = 46, kPitch = 25, kRow = 23, kFoot = 92;
const SDL_DialogFileFilter kFilters[] = { { "LiveSplit splits", "lss" }, { "The timer's own file", "txt" } };

ImVec2 g_box0, g_box1; //where it was drawn in the game's window
std::atomic<bool> g_dragging{ false }; //pointer.cpp asks from the game thread
ImVec2 g_grab; //the cursor's place in the box as it was taken

//its own window
SDL_Window* g_win = nullptr;
ImGuiContext* g_ctx = nullptr;
ImFont* g_popFont = nullptr;
Clock::time_point g_popDrawn{};

ImU32 Fade(ImU32 c, float a) { return (c & ~IM_COL32_A_MASK) | ((ImU32)((float)(c >> IM_COL32_A_SHIFT) * a) << IM_COL32_A_SHIFT); }

//1:23.45, 1:02:03.45 past an hour, 12.34 under a minute
std::string Hms(float s, int decimals = 2)
{
	//rounded first, so 59.996 shows as 1:00.00 and not 60.00
	double scale = std::pow(10.0, decimals);
	double t = std::round(std::max((double)s, 0.0) * scale) / scale;
	int whole = (int)t, h = whole / 3600, m = whole / 60 % 60;
	double sec = t - (double)(h * 3600 + m * 60);
	int width = decimals ? decimals + 3 : 2; //"05.4", or "05" with no decimals
	char text[32];
	if (h) std::snprintf(text, sizeof text, "%d:%02d:%0*.*f", h, m, width, decimals, sec);
	else if (m) std::snprintf(text, sizeof text, "%d:%0*.*f", m, width, decimals, sec);
	else std::snprintf(text, sizeof text, "%.*f", decimals, sec);
	return text;
}

std::string Delta(float d)
{
	return (d < 0 ? "-" : "+") + Hms(std::fabs(d), 1);
}

void RightText(ImDrawList* dl, float right, float y, float size, ImU32 colour, const std::string& s, bool shadow = true)
{
	menu_text(dl, ImVec2(right - menu_text_width(s, size), y), size, colour, s, shadow);
}

float BoxHeight(float u) { return (kHead + kPitch * (float)kSplits + kFoot) * u; }

float Scale()
{
	ImVec2 screen = ImGui::GetIO().DisplaySize;
	return std::max(0.75f, std::min(screen.y / 720, screen.x / 960));
}

//the box with its top left corner at x, y, under the lock
void DrawBox(ImDrawList* dl, float x, float y, float u)
{
	float w = kWidth * u, h = BoxHeight(u);
	ImVec2 b0(x, y), b1(x + w, y + h);

	//the menus' dark screen with its faint grid, and a cream edge
	dl->AddRectFilled(b0, b1, kPanel, 4 * u);
	dl->PushClipRect(b0, b1, true);
	for (float gx = x + 32 * u; gx < x + w; gx += 32 * u) dl->AddLine(ImVec2(gx, y), ImVec2(gx, y + h), kGrid, u);
	for (float gy = y + 32 * u; gy < y + h; gy += 32 * u) dl->AddLine(ImVec2(x, gy), ImVec2(x + w, gy), kGrid, u);
	dl->PopClipRect();
	dl->AddRect(b0, b1, Fade(kCream, g_dragging ? 0.9f : 0.35f), 4 * u, 0, 1.5f * u);

	//the title as the menus have it: a node and a blue bar under a cream one
	ImVec2 node(x + 26 * u, y + 23 * u);
	dl->AddRectFilled(ImVec2(x + 26 * u, y + 9 * u), ImVec2(x + w - 8 * u, y + 12 * u), kCream);
	dl->AddRectFilledMultiColor(ImVec2(x + 26 * u, y + 15 * u), ImVec2(x + w - 8 * u, y + 35 * u), kBlue, Fade(kBlue, 0.15f), Fade(kBlue, 0.15f), kBlue);
	dl->AddRectFilled(ImVec2(x + 26 * u, y + 37 * u), ImVec2(x + w - 8 * u, y + 39 * u), kCream);
	dl->AddCircleFilled(node, 17 * u, kCream, 32);
	dl->AddCircleFilled(node, 13 * u, kBlue, 32);
	dl->AddCircleFilled(node, 4 * u, kCream, 16);
	dl->AddCircleFilled(node, 2 * u, kDark, 12);
	menu_text(dl, ImVec2(x + 50 * u, y + 11 * u), 27 * u, kCream, "Speedrun", true);
	RightText(dl, x + w - 14 * u, y + 15 * u, 19 * u, kCream, "Attempt " + std::to_string(m_Attempts));

	//a row a mission: its name, the difference to the personal best, the time at its end
	float total = 0;
	for (int i = 0; i < kSplits; ++i) total += m_Segment[i];
	float at = 0, sumBest = 0;
	bool haveSum = true;
	for (int i = 0; i < kSplits; ++i)
	{
		at += m_Segment[i];
		if (m_Best[i] < 0) haveSum = false;
		else sumBest += m_Best[i];
		bool current = m_Running && i == m_Index, done = i < m_Index;
		float y0 = y + (kHead + kPitch * (float)i) * u, y1 = y0 + kRow * u;
		dl->AddRectFilled(ImVec2(x + 8 * u, y0), ImVec2(x + w - 8 * u, y1), current ? kSelected : Fade(kBar, done ? 1.0f : 0.7f));
		ImU32 colour = current ? kSelectedText : done ? kCream : Fade(kCream, 0.8f);
		float size = 21 * u, ty = y0 + 1 * u;
		menu_text(dl, ImVec2(x + 16 * u, ty), size, colour, kMissions[i].name, !current);
		std::string time = done || current ? Hms(at) : m_PbBefore[i] >= 0 ? Hms(m_PbBefore[i]) : "-";
		RightText(dl, x + w - 16 * u, ty, size, colour, time, !current);
		float pb = m_PbBefore[i];
		bool showDelta = pb >= 0 && (done || (current && at > pb));
		if (showDelta)
		{
			float d = at - pb;
			ImU32 c = done && m_Gold[i] ? kGoldColour : d < 0 ? kAhead : kBehind;
			RightText(dl, x + w - 112 * u, ty, size, c, Delta(d), !current);
		}
		else if (done && m_Gold[i]) RightText(dl, x + w - 112 * u, ty, size, kGoldColour, "Best", !current);
	}

	//the foot: a cream line, the run's time, the sum of best
	float fy = y + (kHead + kPitch * (float)kSplits + 6) * u;
	ImU32 none = Fade(kCream, 0);
	dl->AddRectFilledMultiColor(ImVec2(x + 8 * u, fy), ImVec2(x + 60 * u, fy + 2 * u), none, kCream, kCream, none);
	dl->AddRectFilled(ImVec2(x + 60 * u, fy), ImVec2(x + w - 60 * u, fy + 2 * u), kCream);
	dl->AddRectFilledMultiColor(ImVec2(x + w - 60 * u, fy), ImVec2(x + w - 8 * u, fy + 2 * u), kCream, none, none, kCream);
	ImU32 clock = kCream;
	if (m_Finished) clock = m_NewPb ? kGoldColour : kBehind;
	else if (m_Running)
	{
		float pb = m_PbBefore[std::min(m_Index, kSplits - 1)];
		clock = pb < 0 || total <= pb ? kAhead : kBehind;
	}
	else clock = Fade(kCream, 0.7f);
	RightText(dl, x + w - 14 * u, fy + 6 * u, 52 * u, clock, Hms(total));
	menu_text(dl, ImVec2(x + 16 * u, fy + 14 * u), 18 * u, Fade(kCream, 0.85f), "Sum of Best", true);
	menu_text(dl, ImVec2(x + 16 * u, fy + 34 * u), 20 * u, kCream, haveSum ? Hms(sumBest) : "-", true);
	const char* state = m_Finished ? (m_NewPb ? "New personal best!" : "Run finished") : m_Running ? "In-game time" : "Pick Dam to start";
	menu_text(dl, ImVec2(x + 16 * u, fy + 62 * u), 18 * u, Fade(kCream, 0.7f), state, true);
}

//the own window has a thread of its own with a GL context of its own: the game's thread only
//builds the picture's vertices (the second ImGui context) and hands them over. drawn with the
//game's context made current on it instead, the switch of drawable cost the game 143 frames a
//second down to 117 on X11; a software renderer before that (its surface a GL renderer's) 150
//to 105, and visible its presents waited for the screen's refresh, so the game ran at 60
struct Picture
{
	int w{}, h{};
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
	bool Start(SDL_Window* pWindow, const unsigned char* font, int fontW, int fontH)
	{
		m_pWindow = pWindow;
		m_Font.assign(font, font + (size_t)fontW * fontH * 4);
		m_FontW = fontW;
		m_FontH = fontH;
		SDL_Window* game = SDL_GL_GetCurrentWindow();
		SDL_GLContext gameContext = SDL_GL_GetCurrentContext();
		m_Context = SDL_GL_CreateContext(pWindow);
		if (game && gameContext) SDL_GL_MakeCurrent(game, gameContext);
		if (!m_Context) return false;
		m_Thread = SDL_CreateThread([](void* me) { static_cast<PopRenderer*>(me)->Run(); return 0; }, "speedrun timer", this);
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
		float l = 0, r = (float)p.w, t = 0, b = (float)p.h;
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
			gl.Scissor((GLint)c.clip.x, (GLint)((float)p.h - c.clip.w), (GLsizei)(c.clip.z - c.clip.x), (GLsizei)(c.clip.w - c.clip.y));
			gl.DrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)c.count, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
				(void*)(size_t)(c.idxAt * sizeof(ImDrawIdx)), (GLint)c.vtxAt);
		}
		static bool shot = false;
		if (const char* path = std::getenv("GE_SPEEDRUN_SHOT"); path && !shot)
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
PopRenderer g_pop;

void OpenPop()
{
	int w = m_WinW > 0 ? m_WinW : (int)kWidth, h = m_WinH > 0 ? m_WinH : (int)BoxHeight(1);
	SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | (std::getenv("WIIKIT_HIDDEN") ? SDL_WINDOW_HIDDEN : 0);
	g_win = SDL_CreateWindow("Speedrun Timer", w, h, flags);
	if (!g_win)
	{
		rt_log("goldeneye: speedrun: no window of its own: %s", SDL_GetError());
		m_Popped = false;
		return;
	}
	if (m_WinX != INT_MIN && m_WinY != INT_MIN) SDL_SetWindowPosition(g_win, m_WinX, m_WinY);
	window_icon(g_win);
	ImGuiContext* game = ImGui::GetCurrentContext();
	g_ctx = ImGui::CreateContext();
	ImGui::SetCurrentContext(g_ctx);
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
	io.Fonts->AddFontDefault();
	g_popFont = menu_font_add(io.Fonts);
	unsigned char* pixels = nullptr;
	int fw = 0, fh = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &fw, &fh);
	io.Fonts->SetTexID((ImTextureID)1); //the thread's only texture
	bool started = g_pop.Start(g_win, pixels, fw, fh);
	ImGui::SetCurrentContext(game);
	if (!started)
	{
		rt_log("goldeneye: speedrun: its window can't be drawn: %s", SDL_GetError());
		g_pop.Stop();
		ImGui::DestroyContext(g_ctx);
		SDL_DestroyWindow(g_win);
		g_ctx = nullptr;
		g_win = nullptr;
		m_Popped = false;
	}
}

void ClosePop()
{
	g_pop.Stop();
	ImGui::DestroyContext(g_ctx);
	SDL_DestroyWindow(g_win);
	g_ctx = nullptr;
	g_win = nullptr;
	g_popFont = nullptr;
}

void DrawPop()
{
	auto now = Clock::now();
	if (now - g_popDrawn < std::chrono::milliseconds(16)) return; //60 a second is plenty for a clock
	g_popDrawn = now;
	ImGuiContext* game = ImGui::GetCurrentContext();
	ImGui::SetCurrentContext(g_ctx);
	Picture p;
	SDL_GetWindowSizeInPixels(g_win, &p.w, &p.h);
	p.w = std::max(p.w, 1);
	p.h = std::max(p.h, 1);
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2((float)p.w, (float)p.h);
	io.DeltaTime = 1.0f / 60;
	ImGui::NewFrame();
	float u = std::min(io.DisplaySize.x / kWidth, io.DisplaySize.y / BoxHeight(1));
	menu_font_select(g_popFont);
	DrawBox(ImGui::GetForegroundDrawList(), std::floor((io.DisplaySize.x - kWidth * u) / 2), std::floor((io.DisplaySize.y - BoxHeight(u)) / 2), u);
	menu_font_select(nullptr);
	ImGui::Render();
	const ImDrawData* d = ImGui::GetDrawData();
	for (int n = 0; n < d->CmdListsCount; ++n)
	{
		const ImDrawList* list = d->CmdLists[n];
		unsigned vtxBase = (unsigned)p.vtx.size(), idxBase = (unsigned)p.idx.size();
		p.vtx.insert(p.vtx.end(), list->VtxBuffer.begin(), list->VtxBuffer.end());
		p.idx.insert(p.idx.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());
		for (const ImDrawCmd& c : list->CmdBuffer)
		{
			if (!c.UserCallback) p.cmds.push_back({ c.ClipRect, c.ElemCount, idxBase + c.IdxOffset, vtxBase + c.VtxOffset });
		}
	}
	ImGui::SetCurrentContext(game);
	g_pop.Show(std::move(p));
}

//the window an event is for, 0 for one that isn't any window's
SDL_WindowID EventWindow(const SDL_Event* e)
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

//its own window's: closed it goes back in the game's, moved or resized it's kept
bool PopEvent(const SDL_Event* e)
{
	std::lock_guard<std::mutex> hold(g_lock);
	switch (e->type)
	{
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		m_Popped = false;
		Save();
		break;
	case SDL_EVENT_WINDOW_MOVED:
		m_WinX = e->window.data1;
		m_WinY = e->window.data2;
		Save();
		break;
	case SDL_EVENT_WINDOW_RESIZED:
		m_WinW = e->window.data1;
		m_WinH = e->window.data2;
		Save();
		break;
	default: break;
	}
	return e->type != SDL_EVENT_KEY_DOWN && e->type != SDL_EVENT_KEY_UP; //F1 and the hotkeys work from it too
}

void Exported(void*, const char* const* files, int)
{
	std::lock_guard<std::mutex> hold(g_lock);
	if (!files) return Say(std::string("Export failed: ") + SDL_GetError());
	if (!files[0]) return;
	std::filesystem::path path = files[0];
	if (!path.has_extension()) path += ".lss";
	std::ofstream f(path);
	f << WriteLss();
	Say(f.good() ? "Exported to " + path.filename().string() : "Couldn't write " + path.string());
}

void Imported(void*, const char* const* files, int)
{
	std::lock_guard<std::mutex> hold(g_lock);
	if (!files) return Say(std::string("Import failed: ") + SDL_GetError());
	if (!files[0]) return;
	std::ifstream f(files[0]);
	std::stringstream text;
	text << f.rdbuf();
	std::string s = text.str();
	std::filesystem::path path = files[0];
	int read = 0;
	if (s.find("<Run") != std::string::npos) read = ReadLss(s);
	else
	{
		float pb[kSplits], best[kSplits];
		std::copy(m_Pb, m_Pb + kSplits, pb);
		std::copy(m_Best, m_Best + kSplits, best);
		Clear();
		std::istringstream in(s);
		read = ReadOwn(in, true);
		if (!read) //not one of ours: what was there stays
		{
			std::copy(pb, pb + kSplits, m_Pb);
			std::copy(best, best + kSplits, m_Best);
		}
	}
	if (!read) return Say("No splits in " + path.filename().string());
	ResetRun();
	Save();
	Say("Imported " + std::to_string(read) + " splits");
}

} //namespace

void speedrun_export()
{
	SDL_ShowSaveFileDialog(Exported, nullptr, SDL_GL_GetCurrentWindow(), kFilters, 1, nullptr);
}

void speedrun_import()
{
	SDL_ShowOpenFileDialog(Imported, nullptr, SDL_GL_GetCurrentWindow(), kFilters, 2, nullptr, false);
}

bool speedrun_event(const SDL_Event* e)
{
	if (g_win && EventWindow(e) == SDL_GetWindowID(g_win)) return PopEvent(e);
	if (!speedrun_timer() || speedrun_window()) return false;
	bool free = !in_play() || overlay_open();
	switch (e->type)
	{
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (!free || e->button.button != SDL_BUTTON_LEFT) return false;
		if (e->button.x < g_box0.x || e->button.x > g_box1.x || e->button.y < g_box0.y || e->button.y > g_box1.y) return false;
		g_dragging = true;
		g_grab = ImVec2(e->button.x - g_box0.x, e->button.y - g_box0.y);
		return true;
	case SDL_EVENT_MOUSE_MOTION:
		if (!g_dragging) return false;
		if (!(e->motion.state & SDL_BUTTON_LMASK) || !free)
		{
			g_dragging = false;
			return false;
		}
		{
			ImVec2 screen = ImGui::GetIO().DisplaySize;
			std::lock_guard<std::mutex> hold(g_lock);
			m_X = (e->motion.x - g_grab.x) / std::max(screen.x, 1.0f);
			m_Y = (e->motion.y - g_grab.y) / std::max(screen.y, 1.0f);
		}
		return true;
	case SDL_EVENT_MOUSE_BUTTON_UP:
	{
		if (!g_dragging || e->button.button != SDL_BUTTON_LEFT) return false;
		g_dragging = false;
		std::lock_guard<std::mutex> hold(g_lock);
		//let go outside the game's window: a window of its own where it was dropped
		SDL_Window* game = SDL_GetWindowFromID(e->button.windowID);
		int ww = 0, wh = 0, wx = 0, wy = 0;
		if (game) SDL_GetWindowSize(game, &ww, &wh);
		if (game && (e->button.x < 0 || e->button.y < 0 || e->button.x >= (float)ww || e->button.y >= (float)wh))
		{
			SDL_GetWindowPosition(game, &wx, &wy);
			m_WinX = wx + (int)(e->button.x - g_grab.x);
			m_WinY = wy + (int)(e->button.y - g_grab.y);
			m_WinW = (int)(g_box1.x - g_box0.x);
			m_WinH = (int)(g_box1.y - g_box0.y);
			m_Popped = true;
			m_X = -1; //back in the game's window it's against the right edge again
			m_Y = 0.06f;
		}
		Save();
		return true;
	}
	default:
		return false;
	}
}

bool speedrun_dragging() { return g_dragging; }

//before the game's window's overlay: its own window opened, closed and drawn
void speedrun_own_window()
{
	bool want;
	{
		std::lock_guard<std::mutex> hold(g_lock);
		EnsureLoaded();
		want = m_Shown && m_Popped;
		if (want && !g_win && menu_font_ready()) OpenPop();
	}
	if (!want && g_win) ClosePop();
	if (!g_win) return;
	std::lock_guard<std::mutex> hold(g_lock);
	DrawPop();
}

bool speedrun_in_game() //drawn over the game's picture
{
	std::lock_guard<std::mutex> hold(g_lock);
	EnsureLoaded();
	return m_Shown && !m_Popped;
}

void speedrun_draw()
{
	if (!menu_font_ready() || pictures_hiding()) return;
	std::lock_guard<std::mutex> hold(g_lock);
	EnsureLoaded();
	if (!m_Shown || m_Popped) return;
	float u = Scale();
	ImVec2 screen = ImGui::GetIO().DisplaySize;
	float w = kWidth * u, h = BoxHeight(u);
	float x = m_X < 0 ? screen.x - w - 16 * u : m_X * screen.x, y = m_Y * screen.y;
	if (!g_dragging) //held, it goes past the edges and out
	{
		x = std::clamp(x, 0.0f, std::max(0.0f, screen.x - w));
		y = std::clamp(y, 0.0f, std::max(0.0f, screen.y - h));
	}
	g_box0 = ImVec2(x, y);
	g_box1 = ImVec2(x + w, y + h);
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	int from = dl->VtxBuffer.Size;
	DrawBox(dl, x, y, u);
	float a = g_dragging ? 1.0f : m_Opacity / 100; //held it's all there, to see where it goes
	if (a < 1)
	{
		for (int i = from; i < dl->VtxBuffer.Size; ++i) dl->VtxBuffer[i].col = Fade(dl->VtxBuffer[i].col, a);
	}
}
#endif

} //namespace ge
