//skip movies (off by default): a movie that plays once is over before it starts
//
//every movie is a widget (made by fn_800DCD10 or playMoive_FUN_800dd960) that play_thp_Movie
//starts through its ge_movie_start, which opens the .thp and clears the widget's "finished"
//byte. what waits for a movie (the widget's own update, movieFinishedPlaying) only reads that
//byte, so with it set and nothing opened the movie has ended the way a played one does: the
//briefings, the movies between and after missions (the end of Dam has no skip button of its
//own), the logos at boot
//a movie that loops (the menus' background) is left alone, it never ends
//
//GE_SKIP_MOVIES=1 on
//GE_TRACE=1 log every movie that starts
#include "ge.h"
#include <atomic>
#include <cstdlib>

namespace ge
{
namespace
{

const uint32_t kMovieFinished = 0x8E; //a byte of the widget, what the game asks to know a movie is over

std::atomic<bool> g_skip_movies{false};
bool g_trace = false;
PPCFunc orig_movie_start = nullptr;

//(widget, path, x, y, width, height, audio track, loops)
void MovieStart(PPCContext& c)
{
	uint32_t widget = c.r[3];
	bool loops = c.r[10] != 0;
	bool skip = g_skip_movies && !loops;
	if (g_trace || skip) rt_log("goldeneye: movie %s%s%s", (const char*)PPC_MEM(c.r[4]), loops ? " (loops)" : "", skip ? ", skipped" : "");
	if (!skip)
	{
		orig_movie_start(c);
		return;
	}
	st8(widget + kMovieFinished, 1);
}

} //namespace

bool skip_movies() { return g_skip_movies; }
void set_skip_movies(bool on) { g_skip_movies = on; }

void install_movies()
{
	if (const char* e = std::getenv("GE_SKIP_MOVIES")) g_skip_movies = std::atoi(e) != 0;
	g_trace = std::getenv("GE_TRACE") != nullptr;
	orig_movie_start = ppc_hook("ge_movie_start", MovieStart);
	if (!orig_movie_start) rt_die("goldeneye: the movie's start is not hooked in this build");
}

} //namespace ge
