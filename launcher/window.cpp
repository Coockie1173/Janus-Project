#include "window.h"
#include "extract.h"
#include "update.h"
#include "ge_font.h"
#include "ge_version.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace ge
{
	void window_icon(SDL_Window* win);
}

namespace launcher
{
namespace
{
	//the f1 menu's colours (port/ui/settings/draw.cpp)
	const ImU32 kCream{ IM_COL32(243, 232, 208, 255) };
	const ImU32 kBlue{ IM_COL32(88, 124, 170, 255) };
	const ImU32 kBack{ IM_COL32(6, 14, 18, 255) };
	const ImU32 kGrid{ IM_COL32(90, 150, 150, 22) };
	const ImU32 kRed{ IM_COL32(230, 120, 100, 255) };

	//the three gl calls the window makes itself, imgui's backend loads its own
	struct GlCalls
	{
		void (*Viewport)(int, int, int, int){};
		void (*ClearColor)(float, float, float, float){};
		void (*Clear)(unsigned){};
		void (*ReadPixels)(int, int, int, int, unsigned, unsigned, void*){};
	};

	//GE_LAUNCHER_SHOT=file.bmp: the window into that file every second, for checking it unattended
	void Shot(const GlCalls& Gl, const char* pPath, int Width, int Height)
	{
		SDL_Surface* pSurface{ SDL_CreateSurface(Width, Height, SDL_PIXELFORMAT_ABGR8888) };
		if (pSurface == nullptr)
		{
			return;
		}
		Gl.ReadPixels(0, 0, Width, Height, 0x1908, 0x1401, pSurface->pixels); //GL_RGBA, GL_UNSIGNED_BYTE
		SDL_FlipSurface(pSurface, SDL_FLIP_VERTICAL);
		SDL_SaveBMP(pSurface, pPath);
		SDL_DestroySurface(pSurface);
	}

	const SDL_DialogFileFilter kFilters[]{ { "Wii disc images", "rvz;iso;wbfs;wia" }, { "All files", "*" } };

	struct State
	{
		std::mutex Lock{};
		std::string Picked{}; //from the file dialog's thread or a drop
		std::string Image{};
		std::string About{};
		std::string Error{};
		std::string File{};
		std::thread Worker{};
		std::atomic<bool> Busy{};
		std::atomic<bool> Done{};
		std::atomic<bool> Cancel{};
		std::atomic<uint64_t> Progress{};
		std::atomic<uint64_t> Total{};
	};

	void OnPicked(void* pUser, const char* const* ppFiles, int)
	{
		State& S{ *(State*)pUser };
		if (ppFiles != nullptr && ppFiles[0] != nullptr)
		{
			std::lock_guard<std::mutex> Guard{ S.Lock };
			S.Picked = ppFiles[0];
		}
	}

	void Start(State& S, const std::string& Image)
	{
		std::string About, Error;
		if (!InspectImage(Image, About, Error))
		{
			S.Error = Image + ": " + Error;
			return;
		}
		S.Image = Image;
		S.About = About;
		S.Error.clear();
		S.Progress = 0;
		S.Total = 0;
		S.Cancel = false;
		S.Busy = true;
		S.Worker = std::thread([&S, Image]
			{
				std::string Error;
				const bool Ok{ ExtractGame(Image, "game", false, [&S](uint64_t Done, uint64_t Total, const std::string& File)
					{
						S.Progress = Done;
						S.Total = Total;
						std::lock_guard<std::mutex> Guard{ S.Lock };
						S.File = File;
						return !S.Cancel;
					}, Error) };
				std::lock_guard<std::mutex> Guard{ S.Lock };
				S.Error = Ok ? "" : S.Cancel ? "Stopped, nothing was kept." : Error;
				S.Done = Ok;
				S.Busy = false;
			});
	}

	bool Button(const char* pLabel, float Width)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, kBlue);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(110, 150, 200, 255));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70, 100, 140, 255));
		const bool Pressed{ ImGui::Button(pLabel, ImVec2(Width, 0)) };
		ImGui::PopStyleColor(3);
		return Pressed;
	}

	//the page both windows draw on: the grid, the title's bars, the version at the bottom
	struct Page
	{
		ImVec2 Size{};
		float u{};
		float Left{};
		float Width{};
	};

	Page BeginPage()
	{
		const ImVec2 Size{ ImGui::GetIO().DisplaySize };
		const float u{ Size.y / 600 };
		ImDrawList* pDraw{ ImGui::GetBackgroundDrawList() };
		for (float x{ 0 }; x < Size.x; x += 48 * u)
		{
			pDraw->AddLine(ImVec2(x, 0), ImVec2(x, Size.y), kGrid, u);
		}
		for (float y{ 0 }; y < Size.y; y += 48 * u)
		{
			pDraw->AddLine(ImVec2(0, y), ImVec2(Size.x, y), kGrid, u);
		}
		//the title's bars, like the f1 menu's
		pDraw->AddRectFilled(ImVec2(0, 92 * u), ImVec2(Size.x, 95 * u), kCream);
		pDraw->AddRectFilledMultiColor(ImVec2(Size.x * 0.45f, 62 * u), ImVec2(Size.x, 84 * u), IM_COL32(88, 124, 170, 0), kBlue, kBlue, IM_COL32(88, 124, 170, 0));

		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(Size);
		ImGui::Begin("launcher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		const float Left{ 48 * u }, Width{ Size.x - 96 * u };
		ImGui::SetCursorPos(ImVec2(Left, 40 * u));
		ImGui::SetWindowFontScale(1.6f);
		ImGui::TextUnformatted("Janus-Project");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::SetCursorPos(ImVec2(Left, 120 * u));
		ImGui::PushTextWrapPos(Left + Width);
		return Page{ Size, u, Left, Width };
	}

	void EndPage(const Page& P)
	{
		ImGui::PopTextWrapPos();
		const char* pVersion{ "Janus-Project " GE_VERSION };
		ImGui::SetCursorPos(ImVec2(P.Size.x - P.Left - ImGui::CalcTextSize(pVersion).x * 0.8f, P.Size.y - 36 * P.u));
		ImGui::SetWindowFontScale(0.8f);
		ImGui::TextDisabled("%s", pVersion);
		ImGui::SetWindowFontScale(1.0f);
		ImGui::End();
	}

	void Draw(SDL_Window* pWindow, State& S, bool& Quit)
	{
		const Page P{ BeginPage() };
		const float u{ P.u }, Left{ P.Left }, Width{ P.Width };
		if (S.Busy)
		{
			const uint64_t Done{ S.Progress }, Total{ S.Total };
			std::string File;
			{
				std::lock_guard<std::mutex> Guard{ S.Lock };
				File = S.File;
			}
			ImGui::TextUnformatted(("Taking the game's files out of " + S.About + "...").c_str());
			ImGui::Dummy(ImVec2(0, 16 * u));
			ImGui::SetCursorPosX(Left);
			char Text[64];
			std::snprintf(Text, sizeof Text, "%.2f / %.2f GB", Done / 1e9, Total / 1e9);
			ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kBlue);
			ImGui::ProgressBar(Total ? (float)Done / Total : 0.0f, ImVec2(Width, 28 * u), Text);
			ImGui::PopStyleColor();
			ImGui::SetCursorPosX(Left);
			ImGui::TextDisabled("%s", File.c_str());
			ImGui::Dummy(ImVec2(0, 24 * u));
			ImGui::SetCursorPosX(Left);
			if (Button(S.Cancel ? "Stopping..." : "Cancel", 180 * u))
			{
				S.Cancel = true;
			}
		}
		else
		{
			ImGui::TextUnformatted("GoldenEye 007's files aren't here yet. This port doesn't come with the game: pick your own disc image of it "
				"(the US version, SJBE52, as an .rvz, .iso, .wbfs or .wia) and its files get taken out into the game folder. "
				"That's needed once, it usually takes under a minute and 4 GB of room.");
			ImGui::Dummy(ImVec2(0, 20 * u));
			ImGui::SetCursorPosX(Left);
			if (Button("Choose Disc Image...", 260 * u))
			{
				SDL_ShowOpenFileDialog(OnPicked, &S, pWindow, kFilters, 2, nullptr, false);
			}
			ImGui::SameLine();
			if (Button("Quit", 140 * u))
			{
				Quit = true;
			}
			ImGui::SetCursorPosX(Left);
			ImGui::TextDisabled("or drop the image on this window");
			if (!S.Error.empty())
			{
				ImGui::Dummy(ImVec2(0, 16 * u));
				ImGui::SetCursorPosX(Left);
				ImGui::PushStyleColor(ImGuiCol_Text, kRed);
				ImGui::TextUnformatted(S.Error.c_str());
				ImGui::PopStyleColor();
			}
		}
		EndPage(P);
	}

	//the window's frame: SDL and Dear ImGui set up, Frame drawing it until it says false, every event
	//handed to OnEvent first
	bool RunWindow(const std::function<void(const SDL_Event&)>& OnEvent, const std::function<bool(SDL_Window*)>& Frame)
	{
		if (!SDL_Init(SDL_INIT_VIDEO))
		{
			std::fprintf(stderr, "launcher: no window (%s)\n", SDL_GetError());
			return false;
		}
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
		const float Scale{ SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()) > 0 ? SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()) : 1.0f };
		SDL_Window* pWindow{ SDL_CreateWindow("Janus-Project", (int)(960 * Scale), (int)(600 * Scale), SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
			| (std::getenv("WIIKIT_HIDDEN") ? SDL_WINDOW_HIDDEN : 0)) };
		SDL_GLContext Context{ pWindow ? SDL_GL_CreateContext(pWindow) : nullptr };
		if (Context == nullptr)
		{
			std::fprintf(stderr, "launcher: no window (%s)\n", SDL_GetError());
			if (pWindow)
			{
				SDL_DestroyWindow(pWindow);
			}
			SDL_Quit();
			return false;
		}
		ge::window_icon(pWindow);
		SDL_GL_SetSwapInterval(1);

		ImGui::CreateContext();
		ImGuiIO& Io{ ImGui::GetIO() };
		Io.IniFilename = nullptr;
		ImFontConfig Font;
		Font.FontDataOwnedByAtlas = false;
		Io.Fonts->AddFontFromMemoryTTF((void*)kMenuFont, sizeof kMenuFont, 22 * Scale, &Font);
		ImGuiStyle& Style{ ImGui::GetStyle() };
		Style.Colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(kCream);
		Style.Colors[ImGuiCol_TextDisabled] = ImGui::ColorConvertU32ToFloat4(IM_COL32(243, 232, 208, 140));
		Style.Colors[ImGuiCol_FrameBg] = ImGui::ColorConvertU32ToFloat4(IM_COL32(30, 40, 50, 255));
		Style.Colors[ImGuiCol_FrameBgHovered] = ImGui::ColorConvertU32ToFloat4(IM_COL32(45, 60, 75, 255));
		Style.Colors[ImGuiCol_FrameBgActive] = ImGui::ColorConvertU32ToFloat4(IM_COL32(60, 80, 100, 255));
		Style.Colors[ImGuiCol_CheckMark] = ImGui::ColorConvertU32ToFloat4(kCream);
		Style.FramePadding = ImVec2(12 * Scale, 8 * Scale);
		ImGui_ImplSDL3_InitForOpenGL(pWindow, Context);
		ImGui_ImplOpenGL3_Init("#version 330");

		const GlCalls Gl{ (void (*)(int, int, int, int))SDL_GL_GetProcAddress("glViewport"),
			(void (*)(float, float, float, float))SDL_GL_GetProcAddress("glClearColor"), (void (*)(unsigned))SDL_GL_GetProcAddress("glClear"),
			(void (*)(int, int, int, int, unsigned, unsigned, void*))SDL_GL_GetProcAddress("glReadPixels") };
		const char* pShot{ std::getenv("GE_LAUNCHER_SHOT") };
		uint64_t LastShot{};
		bool Open{ true };
		while (Open)
		{
			SDL_Event Event;
			while (SDL_PollEvent(&Event))
			{
				ImGui_ImplSDL3_ProcessEvent(&Event);
				OnEvent(Event);
			}

			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplSDL3_NewFrame();
			ImGui::NewFrame();
			Open = Frame(pWindow);
			ImGui::Render();
			int Width{}, Height{};
			SDL_GetWindowSizeInPixels(pWindow, &Width, &Height);
			const ImVec4 Back{ ImGui::ColorConvertU32ToFloat4(kBack) };
			Gl.Viewport(0, 0, Width, Height);
			Gl.ClearColor(Back.x, Back.y, Back.z, 1.0f);
			Gl.Clear(0x4000); //GL_COLOR_BUFFER_BIT
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
			if (pShot != nullptr && SDL_GetTicks() - LastShot >= 1000)
			{
				LastShot = SDL_GetTicks();
				Shot(Gl, pShot, Width, Height);
			}
			SDL_GL_SwapWindow(pWindow);
		}

		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
		SDL_GL_DestroyContext(Context);
		SDL_DestroyWindow(pWindow);
		SDL_Quit();
		return true;
	}
}

bool ExtractWindow()
{
	State S;
	bool Quit{};
	//GE_LAUNCHER_PICK=image: as if it was picked, for checking it unattended
	if (const char* pPick{ std::getenv("GE_LAUNCHER_PICK") })
	{
		S.Picked = pPick;
	}
	const bool Shown{ RunWindow([&](const SDL_Event& Event)
		{
			if (Event.type == SDL_EVENT_QUIT)
			{
				//mid-extraction too: it's stopped below and nothing is kept
				Quit = true;
			}
			else if (Event.type == SDL_EVENT_DROP_FILE && Event.drop.data != nullptr)
			{
				std::lock_guard<std::mutex> Guard{ S.Lock };
				S.Picked = Event.drop.data;
			}
		}, [&](SDL_Window* pWindow)
		{
			std::string Picked;
			{
				std::lock_guard<std::mutex> Guard{ S.Lock };
				Picked.swap(S.Picked);
			}
			if (!Picked.empty() && !S.Busy)
			{
				if (S.Worker.joinable())
				{
					S.Worker.join();
				}
				Start(S, Picked);
			}
			Draw(pWindow, S, Quit);
			return !Quit && !S.Done;
		}) };
	if (!Shown)
	{
		std::fprintf(stderr, "launcher: the game's files are missing, extract them with --extract IMAGE\n");
	}
	if (S.Worker.joinable())
	{
		S.Cancel = true;
		S.Worker.join();
	}
	return S.Done;
}

namespace
{
	struct UpdateState
	{
		UpdateProgress Progress{};
		std::string Error{};
		std::thread Worker{};
		std::atomic<bool> Busy{};
		std::atomic<bool> Done{};
		bool Modpacks{ true };
	};

	std::string Megabytes(uint64_t Bytes)
	{
		char Text[32];
		std::snprintf(Text, sizeof Text, "%.1f MB", Bytes / 1e6);
		return Text;
	}

	void StartUpdate(UpdateState& S, const Release& R)
	{
		if (S.Worker.joinable())
		{
			S.Worker.join();
		}
		S.Error.clear();
		S.Progress.Cancel = false;
		S.Busy = true;
		S.Worker = std::thread([&S, &R]
			{
				std::string Error;
				const bool Ok{ ApplyUpdate(R, S.Modpacks, S.Progress, Error) };
				std::lock_guard<std::mutex> Guard{ S.Progress.Lock };
				S.Error = Error;
				S.Done = Ok;
				S.Busy = false;
			});
	}

	void DrawUpdate(const Release& R, const std::vector<Asset>& Packs, UpdateState& S, bool& Close)
	{
		const Page P{ BeginPage() };
		const float u{ P.u }, Left{ P.Left }, Width{ P.Width };
		if (S.Busy || S.Done)
		{
			const uint64_t Done{ S.Progress.Done }, Total{ S.Progress.Total };
			std::string Step;
			{
				std::lock_guard<std::mutex> Guard{ S.Progress.Lock };
				Step = S.Progress.Step;
			}
			ImGui::TextUnformatted(S.Done ? "Updated, starting the new version..." : "Downloading the update...");
			ImGui::Dummy(ImVec2(0, 16 * u));
			ImGui::SetCursorPosX(Left);
			const std::string Text{ Megabytes(Done) + " / " + Megabytes(Total) };
			ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kBlue);
			ImGui::ProgressBar(Total ? (float)Done / Total : 0.0f, ImVec2(Width, 28 * u), Text.c_str());
			ImGui::PopStyleColor();
			ImGui::SetCursorPosX(Left);
			ImGui::TextDisabled("%s", Step.c_str());
			ImGui::Dummy(ImVec2(0, 24 * u));
			ImGui::SetCursorPosX(Left);
			if (!S.Done && Button(S.Progress.Cancel ? "Stopping..." : "Cancel", 180 * u))
			{
				S.Progress.Cancel = true;
			}
			EndPage(P);
			return;
		}

		ImGui::TextUnformatted("An update is available, download?");
		ImGui::SetCursorPosX(Left);
		ImGui::TextDisabled("%s, you have %s", R.Title.c_str(), GE_VERSION);
		if (!R.Changes.empty())
		{
			ImGui::Dummy(ImVec2(0, 8 * u));
			ImGui::SetCursorPosX(Left);
			ImGui::TextUnformatted("What's new:");
			ImGui::SetCursorPosX(Left);
			ImGui::BeginChild("changes", ImVec2(Width, 150 * u), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
			ImGui::PushTextWrapPos(Width - 16 * u);
			for (const std::string& Change : R.Changes)
			{
				ImGui::Bullet();
				ImGui::TextUnformatted(Change.c_str());
			}
			ImGui::PopTextWrapPos();
			ImGui::EndChild();
		}
		ImGui::Dummy(ImVec2(0, 8 * u));
		ImGui::SetCursorPosX(Left);
		uint64_t Size{ R.Package.Size };
		if (Packs.empty())
		{
			bool Off{};
			ImGui::BeginDisabled();
			ImGui::Checkbox("Update modpacks as well?", &Off);
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::TextDisabled("(they're up to date)");
		}
		else
		{
			ImGui::Checkbox("Update modpacks as well?", &S.Modpacks);
			std::string Names;
			uint64_t PackSize{};
			for (const Asset& A : Packs)
			{
				Names += (Names.empty() ? "" : ", ") + A.Name.substr(0, A.Name.size() - 4);
				PackSize += A.Size;
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(%s: %s)", Names.c_str(), Megabytes(PackSize).c_str());
			Size += S.Modpacks ? PackSize : 0;
		}
		ImGui::Dummy(ImVec2(0, 16 * u));
		ImGui::SetCursorPosX(Left);
		if (Button(("Download (" + Megabytes(Size) + ")").c_str(), 260 * u))
		{
			StartUpdate(S, R);
		}
		ImGui::SameLine();
		if (Button("Not Now", 160 * u))
		{
			Close = true;
		}
		ImGui::SameLine();
		if (Button("Skip This Version", 220 * u))
		{
			SkipRelease(R.Commit);
			Close = true;
		}
		ImGui::SetCursorPosX(Left);
		ImGui::TextDisabled("Your saves, settings, key bindings and the game's files stay as they are.");
		if (!S.Error.empty())
		{
			ImGui::Dummy(ImVec2(0, 12 * u));
			ImGui::SetCursorPosX(Left);
			ImGui::PushStyleColor(ImGuiCol_Text, kRed);
			ImGui::TextUnformatted(S.Error.c_str());
			ImGui::PopStyleColor();
		}
		EndPage(P);
	}
}

bool UpdateWindow(const Release& R)
{
	const std::vector<Asset> Packs{ ChangedModpacks(R) };
	UpdateState S;
	bool Close{};
	//GE_LAUNCHER_UPDATE=yes|no|skip: that button, for checking it unattended
	const std::string Answer{ std::getenv("GE_LAUNCHER_UPDATE") ? std::getenv("GE_LAUNCHER_UPDATE") : "" };
	if (Answer == "yes")
	{
		StartUpdate(S, R);
	}
	else if (Answer == "skip")
	{
		SkipRelease(R.Commit);
		return false;
	}
	else if (Answer == "no")
	{
		return false;
	}
	uint64_t DoneAt{};
	RunWindow([&](const SDL_Event& Event)
		{
			if (Event.type == SDL_EVENT_QUIT)
			{
				//mid-download too: it's stopped and nothing is changed
				Close = true;
				S.Progress.Cancel = S.Busy.load();
			}
		}, [&](SDL_Window*)
		{
			DrawUpdate(R, Packs, S, Close);
			if (S.Done && DoneAt == 0)
			{
				DoneAt = SDL_GetTicks();
			}
			//the "starting" line shown a moment; closing mid-download stops it
			return !(Close && !S.Busy) && !(S.Done && SDL_GetTicks() - DoneAt > 600);
		});
	if (S.Worker.joinable())
	{
		S.Progress.Cancel = true;
		S.Worker.join();
	}
	if (!S.Error.empty())
	{
		std::fprintf(stderr, "update: %s\n", S.Error.c_str());
	}
	return S.Done;
}
}
