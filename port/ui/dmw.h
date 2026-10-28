//a Dolphin Memory Engine watch list (.dmw), read for the debug overlay
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ge {

struct Watch {
    std::string label;
    uint32_t address = 0; //the value's address, or the first pointer's
    std::vector<uint32_t> offsets; //pointer chain, read a pointer and add the offset for each
    int type = 2; //0 byte, 1 halfword, 2 word, 3 float, 4 double, 5 string, 6 bytes
    bool is_unsigned = false;
    int base = 0; //0 decimal, 1 hexadecimal, 2 octal, 3 binary
    uint32_t length = 0; //of a string or an array of bytes
};
struct WatchGroup {
    std::string name;
    std::vector<Watch> watches;
    std::vector<WatchGroup> groups;
    size_t count() const;
};

bool dmw_load(const std::string& path, WatchGroup& root, std::string& error);
//where the value is right now (pointers followed) and what it looks like as text
bool watch_address(const Watch& w, uint32_t& addr);
std::string watch_value(const Watch& w, uint32_t addr);

} //namespace ge
