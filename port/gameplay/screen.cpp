//the picture's own effects, each a setting: the bloom, a flash grenade's white-out and the film grain
//
//Disable Bloom: ge_bloom (802B4F00) is the bloom pass, run by ge_post_effects (802C31D0) after a
//  viewport's scene: the picture copied at half size, blurred four sizes down and added back
//  over it. left out, nothing else changes (it's what the byte 806E9D6C off does)
//Inverted Flashes: a flash grenade whites the screen with a hud quad (draw_delayed: flags
//  0x40000000, a box 1.17912 by 0.840768, drawn 28.2 ahead of the eye) whose colour is the
//  entry's +0x3C, white with an alpha that goes 85, 170, 255 in three frames and then down by
//  about 1.75 a console frame (some 5 s). in its place the picture of the moment it went off
//  stays on screen with a white glow round its edges, both at the quad's alpha, so they go
//  the way the white would. in a cutscene the white is the game's
//  (the viewport's +0x24 is the game's afterimage of the flash, ge_post_effects' fn_802B5330:
//  each frame the last capture is laid over the scene at that strength and captured again.
//  it stays either way)
//Film Grain: the grain is ge_grain_pass (802C4A50), a pass of ge_post_effects: a 128x128 noise
//  texture laid over the viewport at a place from the game's random numbers, so it moves every
//  frame. its quad goes through ge_screen_quad (802C04E0, the colour in r3) white with an alpha of
//  255 times f5 (0.01: 2) and is drawn added (blend 2): the colour times the texture times that
//  alpha. the alpha has three steps at most, so the slider scales the white instead (the channel's
//  colour register, which ge_material_colour sets white in the pass, isn't what's drawn: scaled,
//  nothing changed)
//
//kept in the settings file (LAUNCH_NOBLOOM, LAUNCH_INVERTFLASH, LAUNCH_GRAIN)
//GE_NO_BLOOM=1, GE_INVERT_FLASH=1, GE_GRAIN=0.5 the same from the environment
#include "ge.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#ifdef GE_IMGUI
#include "gl.h"
#include "imgui.h"
#include <SDL3/SDL.h>
#endif

namespace ge
{
namespace
{

const uint32_t kEntryDrawMtx = 0x4, kEntryMesh = 0x10, kEntryColour = 0x3C;
const uint32_t kMeshFlags = 0x4, kMeshBoxMax = 0x20, kMeshOnHud = 0x40000000;
const float kFlashWide = 1.17912f, kFlashAhead = -28.2f;

PPCFunc orig_bloom = nullptr, orig_grain_pass = nullptr, orig_screen_quad = nullptr;
std::atomic<bool> g_no_bloom{false}, g_invert_flash{false};
std::atomic<float> g_grain{1.0f};
std::atomic<float> g_flash{0}; //the white's alpha, 0 to 1, while it is left out
std::atomic<uint32_t> g_flash_frame{0}; //the game's frame it was last left out in
bool g_in_grain = false; //ge_grain_pass is running, on the game's thread

void GrainPass(PPCContext& c)
{
    float grain = g_grain;
    if (grain <= 0) return;
    g_in_grain = grain < 1;
    orig_grain_pass(c);
    g_in_grain = false;
}

//(colour RGBA in r3, f1..f8 the corners and the texture's place)
void ScreenQuad(PPCContext& c)
{
    if (g_in_grain) {
        uint32_t rgba = c.r[3], scaled = rgba & 0xFF;
        for (int shift = 8; shift < 32; shift += 8) scaled |= (uint32_t)std::lround((rgba >> shift & 0xFF) * (double)g_grain) << shift;
        c.r[3] = scaled;
    }
    orig_screen_quad(c);
}

void Bloom(PPCContext& c)
{
    if (g_no_bloom) return;
    orig_bloom(c);
}

//a hud quad that wide, that far ahead of the eye
bool HudQuad(uint32_t entry, float wide, float ahead)
{
    uint32_t mesh = ld32(entry + kEntryMesh), mtx = ld32(entry + kEntryDrawMtx);
    if (!guest_ptr(mesh) || !guest_ptr(mtx) || !(ld32(mesh + kMeshFlags) & kMeshOnHud)) return false;
    return std::fabs(ldf(mtx + 44) - ahead) < 0.05f && std::fabs(ldf(mesh + kMeshBoxMax) - wide) < 0.0005f;
}

#ifdef GE_IMGUI
//on the renderer's thread: the picture of the flash's first frame
struct Gl
{
    PFNGLGENTEXTURESPROC GenTextures;
    PFNGLBINDTEXTUREPROC BindTexture;
    PFNGLTEXPARAMETERIPROC TexParameteri;
    PFNGLCOPYTEXIMAGE2DPROC CopyTexImage2D;
    PFNGLGETINTEGERVPROC GetIntegerv;
    PFNGLBINDFRAMEBUFFERPROC BindFramebuffer;
    PFNGLACTIVETEXTUREPROC ActiveTexture;
};
Gl gl{};
GLuint g_still = 0;
bool g_frozen = false;

bool LoadGl()
{
    if (gl.GenTextures) return true;
    gl.GenTextures = (PFNGLGENTEXTURESPROC)SDL_GL_GetProcAddress("glGenTextures");
    gl.BindTexture = (PFNGLBINDTEXTUREPROC)SDL_GL_GetProcAddress("glBindTexture");
    gl.TexParameteri = (PFNGLTEXPARAMETERIPROC)SDL_GL_GetProcAddress("glTexParameteri");
    gl.CopyTexImage2D = (PFNGLCOPYTEXIMAGE2DPROC)SDL_GL_GetProcAddress("glCopyTexImage2D");
    gl.GetIntegerv = (PFNGLGETINTEGERVPROC)SDL_GL_GetProcAddress("glGetIntegerv");
    gl.BindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
    gl.ActiveTexture = (PFNGLACTIVETEXTUREPROC)SDL_GL_GetProcAddress("glActiveTexture");
    if (gl.GenTextures && gl.BindTexture && gl.TexParameteri && gl.CopyTexImage2D && gl.GetIntegerv && gl.BindFramebuffer && gl.ActiveTexture) return true;
    gl.GenTextures = nullptr;
    rt_log("goldeneye: Inverted Flashes draw nothing: OpenGL's texture functions weren't found");
    return false;
}

//the window as it is now, the game's picture before the overlay is drawn over it
void Freeze(int w, int h)
{
    GLint texture = 0, unit = 0, read = 0;
    gl.GetIntegerv(GL_ACTIVE_TEXTURE, &unit);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    gl.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    if (!g_still) gl.GenTextures(1, &g_still);
    gl.BindTexture(GL_TEXTURE_2D, g_still);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    gl.CopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 0, 0, w, h, 0);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
    gl.BindTexture(GL_TEXTURE_2D, (GLuint)texture);
    gl.ActiveTexture((GLenum)unit);
}
#endif

} //namespace

//from the mesh draw's hook (fov.cpp): true when the entry is left out
bool screen_draw(PPCContext& c)
{
    uint32_t entry = c.r[3];
    if (!guest_ptr(entry)) return false;
    if (g_invert_flash && !local_in_cutscene() && HudQuad(entry, kFlashWide, kFlashAhead)) {
        g_flash = ld8(entry + kEntryColour + 3) / 255.0f;
        g_flash_frame = ld32(kFrameCount);
        return true;
    }
    return false;
}

#ifdef GE_IMGUI
//the white left out this frame or the last, in play
bool flash_showing()
{
    if (!g_invert_flash || !in_play() || g_flash <= 0 || ld32(kFrameCount) - g_flash_frame > 2) {
        g_frozen = false;
        return false;
    }
    return true;
}

//inside the overlay's frame, before anything else of it is drawn
void flash_draw()
{
    if (!LoadGl()) return;
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 size = io.DisplaySize;
    if (!g_frozen) {
        Freeze((int)(size.x * io.DisplayFramebufferScale.x), (int)(size.y * io.DisplayFramebufferScale.y));
        g_frozen = true;
    }
    int a = std::clamp((int)std::lround(g_flash * 255), 0, 255);
    ImU32 white = IM_COL32(255, 255, 255, a), clear = IM_COL32(255, 255, 255, 0);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddImage((ImTextureID)(intptr_t)g_still, ImVec2(0, 0), size, ImVec2(0, 1), ImVec2(1, 0), IM_COL32(255, 255, 255, a));
    //the glow, from the edges in
    float t = std::min(size.x, size.y) * 0.09f;
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(size.x, t), white, white, clear, clear);
    dl->AddRectFilledMultiColor(ImVec2(0, size.y - t), size, clear, clear, white, white);
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(t, size.y), white, clear, clear, white);
    dl->AddRectFilledMultiColor(ImVec2(size.x - t, 0), size, clear, white, white, clear);
}
#endif

bool no_bloom() { return g_no_bloom; }
void set_no_bloom(bool on) { g_no_bloom = on; }
bool invert_flash() { return g_invert_flash; }
void set_invert_flash(bool on) { g_invert_flash = on; }
float film_grain() { return g_grain; }
void set_film_grain(float amount) { g_grain = std::clamp(amount, 0.0f, 1.0f); }

void install_screen()
{
    std::string saved = saved_setting("GE_NO_BLOOM", "LAUNCH_NOBLOOM");
    if (!saved.empty()) g_no_bloom = std::atoi(saved.c_str()) != 0;
    saved = saved_setting("GE_INVERT_FLASH", "LAUNCH_INVERTFLASH");
    if (!saved.empty()) g_invert_flash = std::atoi(saved.c_str()) != 0;
    saved = saved_setting("GE_GRAIN", "LAUNCH_GRAIN");
    if (!saved.empty()) set_film_grain((float)std::atof(saved.c_str()));
    orig_bloom = ppc_hook("ge_bloom", Bloom);
    orig_grain_pass = ppc_hook("ge_grain_pass", GrainPass);
    orig_screen_quad = ppc_hook("ge_screen_quad", ScreenQuad);
    if (!orig_grain_pass || !orig_screen_quad) rt_log("goldeneye: the grain's pass is not hooked in this build, Film Grain does nothing");
    if (!orig_bloom) rt_log("goldeneye: ge_bloom is not hooked in this build, Disable Bloom does nothing");
}

} //namespace ge
