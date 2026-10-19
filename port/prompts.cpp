//prompts: the game's texts show a controller button, the port shows the key that's bound to it
//
//a text names a button with a tag: <ACT name> the button an action is on in the player's
//layout, <ACT2 name> its second one, <menuSELECT> the menus' confirm. ge_text_format
//(801A6C20) copies a text and has each tag written out: ge_text_action (801A6FF0) for the
//two ACTs, ge_text_tag (801A7400) for the others. a button comes out as one character of
//the font, 0x2701 and up (fn_80199040 has the table), the classic's are kGlyphs below
//
//after either call the classic's characters in what it wrote become the first key in the
//key file's line for that button, "[E]". a button with no key keeps its picture, and so
//does everything when the key file's Input is Pad
//
//in play the hud shows a prompt as one or two button sprites (its elements 0x1C and 0x1D) and
//a text next to them (the string at the hud's +0x1708): ge_hud_prompt (801909D0) with the
//text's hash, ge_hud_prompt_text (80190AF0) with a text already made, quick time events come
//through the first with no text at all. both get called without the buttons, so no sprite is
//shown, and the keys are put in front of the text: "[E] Pick up", "[V] + [G]"
//
//everything else that shows a button (the menus' "Back" and "Select") sets a string to the
//button's character with ge_string_set (800E8DC0), which gets the same treatment
//
//a key is drawn in the game's own highlight colour (the font's colour code 0x2801, what a
//text's <C1> tag gives), so it stands out the way the button's picture did
//
//GE_TRACETEXT=1 log every text the game looks up and every tag written out, once each
#include "ge.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace ge
{
namespace
{

struct Glyph { uint32_t ch; const char* button; }; //the font's character, the key file's name
const Glyph kGlyphs[] = {
	//the classic controller pro's, the dark ones (what the runtime's classic gets)
	{0x2721, "A"}, {0x2722, "B"}, {0x2723, "X"}, {0x2724, "Y"}, {0x2725, "L"}, {0x2726, "R"},
	{0x2727, "ZL"}, {0x2728, "ZR"}, {0x271F, "Minus"}, {0x2720, "Plus"},
	{0x2737, "Down"}, {0x2738, "Left"}, {0x2739, "Right"}, {0x273A, "Up"},
	//the classic controller's, the white ones
	{0x270B, "A"}, {0x270C, "B"}, {0x270D, "X"}, {0x270E, "Y"}, {0x270F, "L"}, {0x2710, "R"},
	{0x2711, "ZL"}, {0x2712, "ZR"}, {0x2709, "Minus"}, {0x270A, "Plus"},
	{0x272F, "Down"}, {0x2730, "Left"}, {0x2731, "Right"}, {0x2732, "Up"}};
struct Shown { const char* key; const char* as; }; //a key's name in the file, and in a prompt
const Shown kShown[] = {
	{"Mouse Left", "Left Click"}, {"Mouse Right", "Right Click"}, {"Mouse Middle", "Middle Click"},
	{"Return", "Enter"}, {"Keypad Enter", "Enter"}};
const char* const kMenuKeys[] = {"Return", "Keypad Enter", "Backspace", "Escape"};
const uint32_t kTags = 0x8057A8A0; //the twelve tags, a pointer each
const uint32_t kButtonGlyph = 0x80199040; //(a button's hash) -> its character in the font
const uint32_t kStringFont = 0x14, kButtonFont = 0x07000033; //a string's font, the one with only buttons
const char kGoldOn = 1, kGoldOff = 2; //in a key's text: the game's highlight colour from here, and back
const uint16_t kGold = 0x2801, kPlain = 0x2800; //the font's colour codes, what <C1> and <C0> are written as
const uint16_t kNoBreak = 0xA0; //a space a label isn't wrapped at
const uint32_t kStickGlyph = 0x2708, kStickGlyphPro = 0x271E; //the left stick, on the classic and on the pro
const char* const kSprintKey = "Extra 1"; //the key file's names
const char* const kForwardKey = "Left Stick Up";
const uint32_t kGlyphFirst = 0x2701, kGlyphLast = 0x273A; //the font's buttons
const uint32_t kTextMsg = 0x800DDFC0; //GetTextMsg: (a text's hash, the answer) -> 1 when it's there
const uint32_t kHudPromptText = 0x1708;
const uint32_t kNoText = 0xFFFFFFFF, kAnyText = 0x43000204; //"Select"
const uint32_t kMostPrompt = 200;
const size_t kMostChars = 96; //of what a tag writes out
const auto kReadEvery = std::chrono::milliseconds(500);

using Clock = std::chrono::steady_clock;
PPCFunc orig_text_msg = nullptr, orig_text_action = nullptr, orig_text_tag = nullptr;
PPCFunc orig_hud_prompt = nullptr, orig_hud_prompt_text = nullptr;
PPCFunc button_glyph = nullptr, orig_string_set = nullptr, text_msg = nullptr;
bool g_trace = false;
std::map<std::string, std::vector<std::string>> g_keys; //a button's name in lower case, its keys
bool g_pad = false; //the key file's Input = Pad

std::string Trim(const std::string& t)
{
	size_t a = t.find_first_not_of(" \t\r"), b = t.find_last_not_of(" \t\r");
	return a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
}

std::string Lower(std::string s)
{
	for (char& c : s) c = (char)SDL_tolower((unsigned char)c);
	return s;
}

//the [Classic Controller] lines of the key file, read again when the file changed
void ReadKeys()
{
	static Clock::time_point next{};
	static std::filesystem::file_time_type read{};
	const std::string& path = video_keys_path();
	if (path.empty() || Clock::now() < next) return;
	next = Clock::now() + kReadEvery;
	std::error_code failed;
	auto written = std::filesystem::last_write_time(path, failed);
	if (failed || written == read) return;
	read = written;
	g_keys.clear();
	g_pad = false;
	std::ifstream file(path, std::ios::binary);
	bool inside = false;
	for (std::string line; std::getline(file, line);)
	{
		line = Trim(line);
		if (line.empty() || line[0] == '#') continue;
		if (line[0] == '[') { inside = Lower(line) == "[classic controller]" || Lower(line) == "[classic]"; continue; }
		size_t eq = line.find('=');
		if (!inside || eq == std::string::npos) continue;
		std::string name = Lower(Trim(line.substr(0, eq))), value = Trim(line.substr(eq + 1));
		if (name == "input") g_pad = Lower(value) == "pad";
		std::stringstream all(value);
		for (std::string key; std::getline(all, key, ',');)
		{
			if (!Trim(key).empty()) g_keys[name].push_back(Trim(key));
		}
	}
}

//what a prompt shows for one of the key file's buttons, empty when it has no key
//a menu's button is on the keys a menu is used with when the button has one of them
std::string KeyOf(const char* button, bool menu)
{
	if (g_pad) return {};
	auto found = g_keys.find(Lower(button));
	if (found == g_keys.end() || found->second.empty()) return {};
	std::string key = found->second.front();
	for (const char* preferred : kMenuKeys)
	{
		auto is = [&](const std::string& k) { return !SDL_strcasecmp(k.c_str(), preferred); };
		if (!menu || std::none_of(found->second.begin(), found->second.end(), is)) continue;
		key = preferred;
		break;
	}
	for (const Shown& s : kShown)
	{
		if (!SDL_strcasecmp(s.key, key.c_str())) key = s.as;
	}
	return kGoldOn + ("[" + key + "]") + kGoldOff;
}

//and for a character of the font, empty when it stays
std::string KeyFor(uint32_t ch, bool menu)
{
	for (const Glyph& g : kGlyphs)
	{
		if (g.ch == ch) return KeyOf(g.button, menu);
	}
	return {};
}

bool IsButton(uint32_t ch, const char* button)
{
	for (const Glyph& g : kGlyphs)
	{
		if (g.ch == ch) return !SDL_strcasecmp(g.button, button);
	}
	return false;
}

//a key's text onto a prompt's characters, the two marks as the font's colour codes
void Append(std::vector<uint16_t>& text, const std::string& key)
{
	for (unsigned char ch : key) text.push_back(ch == kGoldOn ? kGold : ch == kGoldOff ? kPlain : ch);
}

std::string Readable(uint32_t text, size_t most = 400)
{
	std::string out;
	for (size_t i = 0; i < most; ++i)
	{
		uint32_t ch = ld16(text + (uint32_t)i * 2);
		if (!ch) break;
		char b[16];
		if (ch >= 0x20 && ch < 0x7F) out += (char)ch;
		else { std::snprintf(b, sizeof b, "{%X}", ch); out += b; }
	}
	return out;
}

//the count characters a tag wrote at out, with the buttons as keys. returns the new count
uint32_t ShowKeys(uint32_t out, uint32_t count, bool menu)
{
	if (!guest_ptr(out) || count == 0 || count > kMostChars) return count;
	ReadKeys();
	std::vector<uint16_t> text;
	bool changed = false;
	for (uint32_t i = 0; i < count; ++i)
	{
		uint16_t ch = ld16(out + i * 2);
		std::string key = KeyFor(ch, menu);
		if (key.empty()) { text.push_back(ch); continue; }
		Append(text, key);
		changed = true;
	}
	if (!changed || text.size() > kMostChars) return count;
	for (size_t i = 0; i < text.size(); ++i) st16(out + (uint32_t)i * 2, text[i]);
	st16(out + (uint32_t)text.size() * 2, 0);
	return (uint32_t)text.size();
}

void TracePrompt(uint32_t first, uint32_t second, uint32_t text, const std::string& keys)
{
	static std::set<std::string> seen;
	char line[96];
	std::snprintf(line, sizeof line, "%08X %08X text %08X -> %s", first, second, text, keys.c_str());
	if (seen.insert(line).second) rt_log("text: prompt %s", line);
}

void TraceTag(uint32_t tag, uint32_t out, uint32_t count)
{
	static std::set<std::string> seen;
	std::string line = Readable(tag, 24) + " -> " + (guest_ptr(out) ? Readable(out, count) : std::string());
	if (seen.insert(line).second) rt_log("text: tag %s", line.c_str());
}

//(r4 where to write, r5 the tag in the text, r6 the reader's place, r7 1 for ACT2) -> characters written
void TextAction(PPCContext& c)
{
	uint32_t out = c.r[4], tag = c.r[5];
	orig_text_action(c);
	uint32_t count = c.r[3];
	if (g_trace) TraceTag(tag, out, count);
	c.r[3] = ShowKeys(out, count, false);
}

//(r4 which tag, r5 where to write) -> characters written
void TextTag(PPCContext& c)
{
	uint32_t out = c.r[5], tag = ld32(kTags + c.r[4] * 4);
	orig_text_tag(c);
	if (g_trace) TraceTag(tag, out, c.r[3]);
	c.r[3] = ShowKeys(out, c.r[3], true);
}

//the keys for a prompt's two buttons (fn_801CF680's hashes), empty when one of them has none
//the left stick with a button is the stick pushed forward and the button: the climb and the
//vault, on the stick and b. the port has those on the sprint key (ge_action_update, input.cpp)
//so that's the key shown for b there, then the key that walks forward
std::string PromptKeys(const PPCContext& c, uint32_t first, uint32_t second)
{
	ReadKeys();
	std::string keys;
	bool stick = false;
	for (uint32_t button : {first, second})
	{
		if (!button) continue;
		PPCContext call = c;
		call.r[3] = button;
		button_glyph(call);
		uint32_t ch = call.r[3];
		if (ch == kStickGlyph || ch == kStickGlyphPro) { stick = true; continue; }
		std::string key = KeyFor(ch, false);
		if (stick && IsButton(ch, "B") && !KeyOf(kSprintKey, false).empty()) key = KeyOf(kSprintKey, false);
		if (key.empty()) return {};
		keys += (keys.empty() ? "" : " + ") + key;
	}
	if (!stick) return keys;
	std::string forward = KeyOf(kForwardKey, false);
	if (forward.empty()) return {};
	return keys.empty() ? forward : keys + " + " + forward;
}

void SetTo(PPCContext& c, const std::vector<uint16_t>& text);

//the hud's prompt text set to the keys and what the prompt says (its characters, 0 for nothing)
void PutKeys(PPCContext& c, uint32_t hud, const std::string& keys, uint32_t says)
{
	std::vector<uint16_t> text;
	Append(text, keys);
	if (guest_ptr(says) && ld16(says))
	{
		text.push_back(' ');
		for (uint32_t i = 0; i < kMostPrompt && ld16(says + i * 2); ++i) text.push_back(ld16(says + i * 2));
	}
	PPCContext call = c;
	call.r[3] = hud + kHudPromptText;
	SetTo(call, text);
}

//a text's characters by its hash, 0 when there's none
uint32_t TextOf(const PPCContext& c, uint32_t hash)
{
	if (hash == 0 || hash == kNoText) return 0;
	PPCContext call = c;
	uint32_t stack = c.r[1], frame = stack - 0x40, out = frame + 0x10; //the answer: characters, hash, a word
	st32(frame, stack);
	st32(out, 0);
	call.r[1] = frame;
	call.r[3] = hash;
	call.r[4] = out;
	text_msg(call);
	return (call.r[3] & 0xFF) ? ld32(out) : 0;
}

//(f1 how far down, r3 the hud, r4 r5 the buttons, r6 the text's hash, r7 bright)
void HudPrompt(PPCContext& c)
{
	uint32_t hud = c.r[3], text = c.r[6];
	std::string keys = guest_ptr(hud) ? PromptKeys(c, c.r[4], c.r[5]) : std::string();
	if (g_trace && (c.r[4] || c.r[5])) TracePrompt(c.r[4], c.r[5], text, keys);
	if (keys.empty()) { orig_hud_prompt(c); return; }
	c.r[4] = c.r[5] = 0;
	c.r[6] = kAnyText; //so the text gets placed and shown, what it says comes after
	orig_hud_prompt(c);
	PutKeys(c, hud, keys, TextOf(c, text));
}

//(f1, r3 the hud, r4 r5 the buttons, r6 the text as a string, r7 bright, r8)
void HudPromptText(PPCContext& c)
{
	uint32_t hud = c.r[3];
	std::string keys = guest_ptr(hud) ? PromptKeys(c, c.r[4], c.r[5]) : std::string();
	if (g_trace && (c.r[4] || c.r[5])) TracePrompt(c.r[4], c.r[5], 0, keys);
	if (keys.empty()) { orig_hud_prompt_text(c); return; }
	uint32_t says = guest_ptr(c.r[6]) ? ld32(c.r[6]) : 0;
	c.r[4] = c.r[5] = 0;
	orig_hud_prompt_text(c);
	PutKeys(c, hud, keys, says);
}

//the string at r3 set to these characters, kept in a frame of their own under the stack
void SetTo(PPCContext& c, const std::vector<uint16_t>& text)
{
	uint32_t stack = c.r[1], frame = (stack - 0x20 - ((uint32_t)text.size() + 1) * 2) & ~0xFu, at = frame + 0x10;
	st32(frame, stack);
	for (size_t i = 0; i < text.size(); ++i) st16(at + (uint32_t)i * 2, text[i]);
	st16(at + (uint32_t)text.size() * 2, 0);
	c.r[1] = frame;
	c.r[4] = at;
	orig_string_set(c);
	c.r[1] = stack;
}

//(r3 a hud or menu string, r4 the characters): any button left in what a string is set to
//a menu draws a button and its label with one string, the button first in a font that only
//has buttons: that one is set to nothing and its key goes with the label set next,
//"[Backspace] Back" and "Select [Enter]"
void StringSet(PPCContext& c)
{
	static uint32_t labelString = 0; //the string whose button was left out, and the button
	static uint16_t labelButton = 0;
	uint32_t string = c.r[3], from = c.r[4];
	bool button = false;
	uint32_t count = 0;
	for (; guest_ptr(from) && count < kMostPrompt && ld16(from + count * 2); ++count)
	{
		uint32_t ch = ld16(from + count * 2);
		if (ch >= kGlyphFirst && ch <= kGlyphLast) button = true;
	}
	if (count == kMostPrompt || (!button && string != labelString)) { orig_string_set(c); return; }
	ReadKeys();
	bool menu = !in_play();
	std::vector<uint16_t> text;
	if (!button) //the label of the button before
	{
		std::string key = KeyFor(labelButton, menu);
		bool after = IsButton(labelButton, "A"); //the confirm's label ends at the right, the others start at the left
		labelString = 0;
		if (!after) { Append(text, key); text.push_back(kNoBreak); }
		for (uint32_t i = 0; i < count; ++i) text.push_back(ld16(from + i * 2));
		if (after) { text.push_back(kNoBreak); Append(text, key); }
		SetTo(c, text);
		return;
	}
	if (g_trace) TraceTag(from, from, count);
	if (count == 1 && ld32(string + kStringFont) == kButtonFont && !KeyFor(ld16(from), menu).empty())
	{
		labelString = string;
		labelButton = ld16(from);
		SetTo(c, text);
		return;
	}
	bool changed = false;
	for (uint32_t i = 0; i < count; ++i)
	{
		uint16_t ch = ld16(from + i * 2);
		std::string key = KeyFor(ch, menu);
		if (key.empty()) { text.push_back(ch); continue; }
		Append(text, key);
		changed = true;
	}
	if (!changed) { orig_string_set(c); return; }
	SetTo(c, text);
}

void TextMsg(PPCContext& c)
{
	uint32_t hash = c.r[3], out = c.r[4];
	//the Mods menu's and the custom weapons' texts aren't the game's: characters, the hash, a word
	uint32_t own = mods_menu_text(hash);
	if (!own) own = custom_weapon_text(hash);
	if (own)
	{
		if (guest_ptr(out)) { st32(out, own); st32(out + 4, hash); st32(out + 8, 0); }
		c.r[3] = 1;
		return;
	}
	orig_text_msg(c);
	if (!g_trace) return;
	static std::set<uint32_t> seen;
	if (!guest_ptr(out) || !guest_ptr(ld32(out)) || !seen.insert(hash).second) return;
	rt_log("text: %08X \"%s\" %s", hash, Readable(ld32(out)).c_str(), call_chain(c).c_str());
}

} //namespace

std::string game_text(const PPCContext& c, uint32_t hash)
{
	if (!text_msg) return {};
	const uint32_t text = TextOf(c, hash);
	return text ? Readable(text) : std::string();
}

void install_prompts()
{
	g_trace = std::getenv("GE_TRACETEXT") != nullptr;
	orig_text_msg = ppc_hook("GetTextMsg_MENU_Text", TextMsg);
	orig_text_action = ppc_hook("ge_text_action", TextAction);
	orig_text_tag = ppc_hook("ge_text_tag", TextTag);
	orig_hud_prompt = ppc_hook("ge_hud_prompt", HudPrompt);
	orig_hud_prompt_text = ppc_hook("ge_hud_prompt_text", HudPromptText);
	button_glyph = ppc_lookup(kButtonGlyph);
	text_msg = ppc_lookup(kTextMsg);
	orig_string_set = ppc_hook("ge_string_set", StringSet);
	if (!orig_text_action || !orig_text_tag || !orig_hud_prompt || !orig_hud_prompt_text || !button_glyph || !orig_string_set || !text_msg) rt_die("goldeneye: the prompts are not hooked in this build");
}

} //namespace ge
