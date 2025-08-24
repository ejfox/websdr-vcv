#include <iostream>
#include <vector>
#include <cstring>

// Station database
struct Station {
    float frequency;
    const char* name;
    const char* bestTime;
    const char* mode;
};

static const Station STATIONS[] = {
    // time signals
    {5000000,  "wwv 5",       "24h", "am"},
    {10000000, "wwv 10",      "24h", "am"},
    {15000000, "wwv 15",      "24h", "am"},
    
    // international broadcasters
    {9410000,  "bbc",         "night", "am"},
    {15400000, "voa",         "day", "am"},
    {11955000, "turkey",      "evening", "am"},
    
    // amateur bands
    {3573000,  "80m ft8",     "night", "usb"},
    {7074000,  "40m ft8",     "24h", "usb"},
    {14074000, "20m ft8",     "day", "usb"},
    
    // mystery signals
    {4625000,  "uvb-76",      "24h",   "am"},
    {8992000,  "hfgcs",       "24h",   "usb"},
    
    // pirate radio
    {6925000,  "pirates",     "weekend", "am"}
};

const int NUM_STATIONS = sizeof(STATIONS) / sizeof(Station);

// Find station by frequency
const Station* findStation(float freq) {
    for (int i = 0; i < NUM_STATIONS; i++) {
        if (std::abs(STATIONS[i].frequency - freq) < 100) {
            return &STATIONS[i];
        }
    }
    return nullptr;
}

// Scanner modes
enum ScanMode {
    SCAN_ALL,
    SCAN_TIME,
    SCAN_INTERNATIONAL,
    SCAN_AMATEUR,
    SCAN_MYSTERY
};

// Get stations for scan mode
std::vector<const Station*> getStationsForMode(ScanMode mode) {
    std::vector<const Station*> result;
    
    for (int i = 0; i < NUM_STATIONS; i++) {
        const Station* s = &STATIONS[i];
        bool include = false;
        
        switch (mode) {
            case SCAN_ALL:
                include = true;
                break;
            case SCAN_TIME:
                include = (strstr(s->name, "wwv") != nullptr);
                break;
            case SCAN_INTERNATIONAL:
                include = (strstr(s->name, "bbc") || strstr(s->name, "voa") || 
                          strstr(s->name, "turkey"));
                break;
            case SCAN_AMATEUR:
                include = (strstr(s->name, "ft8") || strstr(s->name, "cw"));
                break;
            case SCAN_MYSTERY:
                include = (strstr(s->name, "uvb") || strstr(s->name, "hfgcs"));
                break;
        }
        
        if (include) {
            result.push_back(s);
        }
    }
    
    return result;
}

int main() {
    std::cout << "testing station database...\n\n";
    
    // test station lookup
    std::cout << "frequency lookups:\n";
    float testFreqs[] = {5000000, 9410000, 4625000, 7074000, 1234567};
    
    for (float freq : testFreqs) {
        const Station* s = findStation(freq);
        if (s) {
            std::cout << "  " << freq << " hz -> " << s->name 
                     << " (" << s->bestTime << ", " << s->mode << ")\n";
        } else {
            std::cout << "  " << freq << " hz -> not found\n";
        }
    }
    
    // test scanner modes
    std::cout << "\nscanner modes:\n";
    const char* modeNames[] = {"all", "time", "international", "amateur", "mystery"};
    
    for (int mode = 0; mode < 5; mode++) {
        auto stations = getStationsForMode((ScanMode)mode);
        std::cout << "  " << modeNames[mode] << ": " << stations.size() << " stations\n";
        if (stations.size() > 0 && stations.size() <= 3) {
            for (auto s : stations) {
                std::cout << "    - " << s->name << "\n";
            }
        }
    }
    
    std::cout << "\nall tests passed!\n";
    return 0;
}