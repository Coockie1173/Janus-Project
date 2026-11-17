//a window of its own beside the game's: an SDL window with an ImGui context of its own, drawn by a
//thread with a GL context of its own (the game's thread only builds the vertices and hands them over)
//
//drawn with the game's GL context made current on it instead, the switch of drawable cost the game
//143 frames a second down to 117 on X11; a software renderer before that (its surface a GL renderer's)
//150 to 105, and visible its presents waited for the screen's refresh, so the game ran at 60
//
//the speedrun timer's (only drawn) and TASLib's editor (the mouse and the keys work it, through
//Dear ImGui's SDL backend on its context)
#pragma once
#ifdef GE_IMGUI
#include "imgui.h"
#include <SDL3/SDL.h>
#include <functional>
#include <memory>

namespace ge
{

class PopRenderer;

class PopWindow
{
public:
	PopWindow();
	~PopWindow();

	//on the renderer's thread. fonts adds the context's fonts (Dear ImGui's own when null); input: the
	//mouse and the keys work it (its events go to Event). x, y INT_MIN: where the system puts it
	bool Open(const char* title, int x, int y, int w, int h, bool input, const std::function<void(ImFontAtlas*)>& fonts = nullptr);
	void Close();
	bool Opened() const { return m_pWindow != nullptr; }
	SDL_Window* Window() const { return m_pWindow; }
	ImGuiContext* Context() const { return m_pContext; }
	bool Focused() const;

	//a frame of it: Begin makes its context current (the one current before is kept) and starts the
	//frame, End renders it, hands it to its thread and puts the other context back
	void Begin();
	void End();

	//an event of this window's: true, and with input it went to its ImGui
	bool Event(const SDL_Event* e);

private:
	SDL_Window* m_pWindow = nullptr;
	ImGuiContext* m_pContext = nullptr;
	ImGuiContext* m_pOuter = nullptr;
	bool m_Input = false;
	std::unique_ptr<PopRenderer> m_pRenderer;
};

SDL_WindowID event_window(const SDL_Event* e); //the window an event is for, 0 none

} //namespace ge
#endif
