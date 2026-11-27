#include "ui/popwindow.h"
#ifdef GE_IMGUI
#include "ge.h"
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

//drawn on the CPU by a thread of its own (SDL's software renderer on a surface), so no GL context
//of a second window ever presents: the Steam overlay and RivaTuner hook the GL presents and break the
//game's picture with two of them on two threads. Shown where the system has a framebuffer for a
//window (Windows' GDI, X11) by a blit on the game's thread; elsewhere (Wayland, macOS) the thread
//uploads it to an SDL renderer of the window's
class PopRenderer
{
public:
	//on the game's thread, before the window's first picture
	bool Start(SDL_Window* pWindow, bool native, const unsigned char* font, int fontW, int fontH, const char* name)
	{
		m_pWindow = pWindow;
		m_Native = native;
		m_Name = name;
		m_Font.assign(font, font + (size_t)fontW * fontH * 4);
		m_FontW = fontW;
		m_FontH = fontH;
		if (m_Native) SDL_SetWindowSurfaceVSync(m_pWindow, SDL_WINDOW_SURFACE_VSYNC_DISABLED);
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
		m_Thread = nullptr;
		SDL_DestroySurface(m_pReady);
		m_pReady = nullptr;
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

	//on the game's thread: the thread's newest picture blitted to the window's framebuffer
	void Present()
	{
		if (!m_Native) return;
		{
			std::lock_guard<std::mutex> hold(m_Lock);
			if (!m_Fresh || !m_pReady) return;
			m_Fresh = false;
			SDL_Surface* screen = SDL_GetWindowSurface(m_pWindow);
			if (!screen)
			{
				if (!m_Said) rt_log("goldeneye: window \"%s\" can't be shown: %s", m_Name.c_str(), SDL_GetError());
				m_Said = true;
				return;
			}
			if (screen->w != m_pReady->w || screen->h != m_pReady->h) SDL_FillSurfaceRect(screen, nullptr, SDL_MapSurfaceRGB(screen, 6, 14, 18));
			SDL_BlitSurface(m_pReady, nullptr, screen, nullptr);
		}
		if (!SDL_UpdateWindowSurface(m_pWindow) && !m_Said)
		{
			rt_log("goldeneye: window \"%s\" can't be shown: %s", m_Name.c_str(), SDL_GetError());
			m_Said = true;
		}
	}

private:
	void Run()
	{
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
			if (!Size(now.w, now.h)) continue;
			Draw(now);
			Finish();
		}
		Drop();
		if (m_pShower) SDL_DestroyRenderer(m_pShower);
		m_pShower = nullptr;
	}

	//the surface it's drawn on at the picture's size, with its renderer and the font on it
	bool Size(int w, int h)
	{
		if (m_pCanvas && m_pCanvas->w == w && m_pCanvas->h == h) return true;
		Drop();
		m_pCanvas = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_XRGB8888);
		m_pDraw = m_pCanvas ? SDL_CreateSoftwareRenderer(m_pCanvas) : nullptr;
		m_pFont = m_pDraw ? SDL_CreateTexture(m_pDraw, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, m_FontW, m_FontH) : nullptr;
		if (!m_pFont)
		{
			rt_log("goldeneye: window \"%s\" can't be drawn: %s", m_Name.c_str(), SDL_GetError());
			Drop();
			return false;
		}
		SDL_UpdateTexture(m_pFont, nullptr, m_Font.data(), m_FontW * 4);
		SDL_SetTextureBlendMode(m_pFont, SDL_BLENDMODE_BLEND);
		SDL_SetTextureScaleMode(m_pFont, SDL_SCALEMODE_LINEAR);
		return true;
	}

	void Drop()
	{
		if (m_pDraw) SDL_DestroyRenderer(m_pDraw); //its texture with it
		SDL_DestroySurface(m_pCanvas);
		m_pDraw = nullptr;
		m_pFont = nullptr;
		m_pCanvas = nullptr;
	}

	void Draw(const Picture& p)
	{
		SDL_SetRenderClipRect(m_pDraw, nullptr);
		SDL_SetRenderDrawColor(m_pDraw, 6, 14, 18, 255);
		SDL_RenderClear(m_pDraw);
		const float sx = (float)p.w / (p.dw > 0 ? p.dw : (float)p.w), sy = (float)p.h / (p.dh > 0 ? p.dh : (float)p.h); //points to pixels
		m_Xy.resize(p.vtx.size() * 2);
		m_Colour.resize(p.vtx.size());
		for (size_t i = 0; i < p.vtx.size(); ++i)
		{
			const ImDrawVert& v = p.vtx[i];
			m_Xy[i * 2] = v.pos.x * sx;
			m_Xy[i * 2 + 1] = v.pos.y * sy;
			m_Colour[i] = { (float)(v.col >> IM_COL32_R_SHIFT & 0xFF) / 255, (float)(v.col >> IM_COL32_G_SHIFT & 0xFF) / 255,
				(float)(v.col >> IM_COL32_B_SHIFT & 0xFF) / 255, (float)(v.col >> IM_COL32_A_SHIFT & 0xFF) / 255 };
		}
		for (const Picture::Cmd& c : p.cmds)
		{
			if (c.clip.z <= c.clip.x || c.clip.w <= c.clip.y || c.vtxAt >= p.vtx.size()) continue;
			SDL_Rect clip{ (int)(c.clip.x * sx), (int)(c.clip.y * sy), (int)((c.clip.z - c.clip.x) * sx), (int)((c.clip.w - c.clip.y) * sy) };
			SDL_SetRenderClipRect(m_pDraw, &clip);
			const ImDrawVert* v = &p.vtx[c.vtxAt];
			SDL_RenderGeometryRaw(m_pDraw, m_pFont, &m_Xy[(size_t)c.vtxAt * 2], sizeof(float) * 2, &m_Colour[c.vtxAt], sizeof(SDL_FColor),
				&v->uv.x, sizeof(ImDrawVert), (int)(p.vtx.size() - c.vtxAt), &p.idx[c.idxAt], (int)c.count, sizeof(ImDrawIdx));
		}
		SDL_FlushRenderer(m_pDraw);
		//GE_SPEEDRUN_SHOT=F.bmp the timer's first picture, GE_POPWINDOW_SHOT=F.bmp the other windows' every 120th
		static bool shot = false;
		const bool timer = m_Name == "speedrun timer";
		if (const char* path = std::getenv(timer ? "GE_SPEEDRUN_SHOT" : "GE_POPWINDOW_SHOT"); path && (timer ? !shot : ++m_Pictures % 120 == 0))
			shot = SDL_SaveBMP(m_pCanvas, path);
	}

	//the picture to the window: kept for the game's thread to blit, or uploaded and presented here
	void Finish()
	{
		if (m_Native)
		{
			std::lock_guard<std::mutex> hold(m_Lock);
			if (!m_pReady || m_pReady->w != m_pCanvas->w || m_pReady->h != m_pCanvas->h)
			{
				SDL_DestroySurface(m_pReady);
				m_pReady = SDL_CreateSurface(m_pCanvas->w, m_pCanvas->h, m_pCanvas->format);
			}
			if (m_pReady && SDL_BlitSurface(m_pCanvas, nullptr, m_pReady, nullptr)) m_Fresh = true;
			return;
		}
		if (!m_pShower)
		{
			m_pShower = SDL_CreateRenderer(m_pWindow, nullptr);
			if (!m_pShower)
			{
				rt_log("goldeneye: window \"%s\" can't be shown: %s", m_Name.c_str(), SDL_GetError());
				m_Native = true; //never shown, but not tried again a picture
				return;
			}
			SDL_SetRenderVSync(m_pShower, 0); //its presents never wait for the screen
		}
		if (!m_pShown || m_pShown->w != m_pCanvas->w || m_pShown->h != m_pCanvas->h)
		{
			if (m_pShown) SDL_DestroyTexture(m_pShown);
			m_pShown = SDL_CreateTexture(m_pShower, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, m_pCanvas->w, m_pCanvas->h);
			if (!m_pShown) return;
		}
		SDL_UpdateTexture(m_pShown, nullptr, m_pCanvas->pixels, m_pCanvas->pitch);
		SDL_RenderTexture(m_pShower, m_pShown, nullptr, nullptr);
		SDL_RenderPresent(m_pShower);
	}

	SDL_Window* m_pWindow{ nullptr };
	bool m_Native{ false };
	std::string m_Name;
	unsigned m_Pictures{ 0 };
	SDL_Thread* m_Thread{ nullptr };
	std::mutex m_Lock;
	std::condition_variable m_Wake;
	Picture m_Next;
	bool m_Have{ false }, m_Quit{ false }, m_Fresh{ false }, m_Said{ false };
	std::vector<unsigned char> m_Font;
	int m_FontW{}, m_FontH{};
	//the thread's
	SDL_Surface* m_pCanvas{ nullptr };
	SDL_Renderer* m_pDraw{ nullptr };
	SDL_Texture* m_pFont{ nullptr };
	std::vector<float> m_Xy;
	std::vector<SDL_FColor> m_Colour;
	SDL_Renderer* m_pShower{ nullptr }; //where there's no framebuffer for a window
	SDL_Texture* m_pShown{ nullptr };
	//the last picture drawn, under the lock
	SDL_Surface* m_pReady{ nullptr };
};

//Windows' and X11's windows have a framebuffer of their own; elsewhere SDL would make one of a GL renderer's
bool NativeFramebuffer()
{
	const char* driver = SDL_GetCurrentVideoDriver();
	const bool native = driver && (!std::strcmp(driver, "windows") || !std::strcmp(driver, "x11"));
	if (native) SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0"); //on X11 SDL would go through GL too
	return native;
}

PopWindow::PopWindow() = default;
PopWindow::~PopWindow() { (void)m_pRenderer.release(); } //at exit: SDL may be gone, nothing is torn down under it

bool PopWindow::Open(const char* title, int x, int y, int w, int h, bool input, const std::function<void(ImFontAtlas*)>& fonts)
{
	if (m_pWindow) return true;
	static const bool native = NativeFramebuffer();
	SDL_WindowFlags flags = (native ? 0 : SDL_WINDOW_OPENGL) | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (std::getenv("WIIKIT_HIDDEN") ? SDL_WINDOW_HIDDEN : 0);
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
	const bool started = m_pRenderer->Start(m_pWindow, native, pixels, fw, fh, title && !std::strcmp(title, "Speedrun Timer") ? "speedrun timer" : "pop window");
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

void PopWindow::Present()
{
	if (m_pRenderer) m_pRenderer->Present();
}

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
