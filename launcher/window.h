//the window a first start opens: the game's files aren't there yet, so a disc image is picked
//and they're taken out of it
#ifndef LAUNCHER_WINDOW_H
#define LAUNCHER_WINDOW_H

namespace launcher
{
	//true once game/ is filled, false when the window is closed first (or can't be opened)
	bool ExtractWindow();
}

#endif
