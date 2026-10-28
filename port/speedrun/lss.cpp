//LiveSplit's splits file (.lss): the personal best and the best segments in game time, in and out

#include "speedrun/splits.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace ge::speedrun
{

//LiveSplit's times: [d.]hh:mm:ss.fffffff
std::string LssTime(float seconds)
{
	double t = seconds;
	int h = (int)(t / 3600), m = (int)(t / 60) % 60;
	char text[32];
	std::snprintf(text, sizeof text, "%02d:%02d:%010.7f", h, m, t - h * 3600.0 - m * 60.0);
	return text;
}

float ReadLssTime(const std::string& text)
{
	int days = 0, h = 0, m = 0;
	double sec = 0;
	std::string t = text;
	size_t colon = t.find(':');
	if (colon == std::string::npos) return -1;
	if (size_t dot = t.find('.'); dot < colon)
	{
		days = std::atoi(t.substr(0, dot).c_str());
		t = t.substr(dot + 1);
	}
	if (std::sscanf(t.c_str(), "%d:%d:%lf", &h, &m, &sec) != 3) return -1;
	return (float)(days * 86400.0 + h * 3600.0 + m * 60.0 + sec);
}

//what's between <tag> and </tag>, empty for none
std::string Tag(const std::string& s, const char* tag)
{
	std::string open = std::string("<") + tag + ">", close = std::string("</") + tag + ">";
	size_t a = s.find(open);
	if (a == std::string::npos) return "";
	a += open.size();
	size_t b = s.find(close, a);
	return b == std::string::npos ? "" : s.substr(a, b - a);
}

//an element that starts at the text "start" (<BestSegmentTime, or the Personal Best's SplitTime):
//its game time, its real time without one, -1 for neither or when it's empty
float TimeOf(const std::string& s, const std::string& start, const char* close)
{
	size_t a = s.find(start);
	if (a == std::string::npos) return -1;
	size_t gt = s.find('>', a);
	if (gt == std::string::npos || s[gt - 1] == '/') return -1;
	size_t b = s.find(close, gt);
	std::string inside = s.substr(gt + 1, b == std::string::npos ? std::string::npos : b - gt - 1);
	std::string game = Tag(inside, "GameTime");
	return ReadLssTime(game.empty() ? Tag(inside, "RealTime") : game);
}

int ReadLss(const std::string& s)
{
	struct Seg
	{
		std::string name;
		float pb, best;
	};
	std::vector<Seg> segs;
	for (size_t at = s.find("<Segment>"); at != std::string::npos; at = s.find("<Segment>", at + 1))
	{
		size_t end = s.find("</Segment>", at);
		if (end == std::string::npos) break;
		std::string seg = s.substr(at, end - at);
		segs.push_back({ Tag(seg, "Name"), TimeOf(seg, "name=\"Personal Best\"", "</SplitTime>"), TimeOf(seg, "<BestSegmentTime", "</BestSegmentTime>") });
	}
	bool byName = false;
	for (const Seg& g : segs) byName |= SplitNamed(g.name) >= 0;
	if (!byName && (int)segs.size() != kSplits) return 0;
	Clear();
	int read = 0;
	for (size_t n = 0; n < segs.size(); ++n)
	{
		int i = byName ? SplitNamed(segs[n].name) : (int)n;
		if (i < 0) continue;
		m_Pb[i] = segs[n].pb;
		m_Best[i] = segs[n].best;
		++read;
	}
	if (std::string count = Tag(s, "AttemptCount"); !count.empty()) m_Attempts = std::atoi(count.c_str());
	return read;
}

std::string WriteLss()
{
	std::ostringstream f;
	f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Run version=\"1.7.0\">\n  <GameIcon />\n"
	  << "  <GameName>GoldenEye 007 (Wii)</GameName>\n  <CategoryName>Any% IGT</CategoryName>\n"
	  << "  <Metadata>\n    <Run id=\"\" />\n    <Platform usesEmulator=\"False\">Wii</Platform>\n    <Region>\n    </Region>\n    <Variables />\n  </Metadata>\n"
	  << "  <Offset>00:00:00</Offset>\n  <AttemptCount>" << m_Attempts << "</AttemptCount>\n  <AttemptHistory />\n  <Segments>\n";
	for (int i = 0; i < kSplits; ++i)
	{
		f << "    <Segment>\n      <Name>" << kMissions[i].name << "</Name>\n      <Icon />\n      <SplitTimes>\n";
		if (m_Pb[i] >= 0) f << "        <SplitTime name=\"Personal Best\">\n          <GameTime>" << LssTime(m_Pb[i]) << "</GameTime>\n        </SplitTime>\n";
		else f << "        <SplitTime name=\"Personal Best\" />\n";
		f << "      </SplitTimes>\n";
		if (m_Best[i] >= 0) f << "      <BestSegmentTime>\n        <GameTime>" << LssTime(m_Best[i]) << "</GameTime>\n      </BestSegmentTime>\n";
		else f << "      <BestSegmentTime />\n";
		f << "      <SegmentHistory />\n    </Segment>\n";
	}
	f << "  </Segments>\n  <AutoSplitterSettings />\n</Run>\n";
	return f.str();
}

} //namespace ge::speedrun
