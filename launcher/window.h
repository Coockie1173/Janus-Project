//the windows before the game: the first start's, the game's files aren't there yet so a disc
//image is picked and they're taken out of it, and the one that offers an update
#ifndef LAUNCHER_WINDOW_H
#define LAUNCHER_WINDOW_H

namespace launcher
{
	struct Release;

	//true once game/ is filled, false when the window is closed first (or can't be opened)
	bool ExtractWindow();
	//"an update is available": true once it's downloaded and in place (update.h), false for
	//not now, skip this version, or a failed download
	bool UpdateWindow(const Release& R);
}

#endif
