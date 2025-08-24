// known shortwave stations that are often active
// frequencies in hz, times in utc

struct Station {
    float freq;
    const char* name;
    const char* time;  // best reception time
    const char* mode;  // am/usb/lsb
};

// verified active stations as of 2024/2025
static const Station STATIONS[] = {
    // time stations (always on)
    {2500000,  "wwv 2.5",     "24h", "am"},    // nist time colorado
    {5000000,  "wwv 5",       "24h", "am"},    // nist time colorado  
    {10000000, "wwv 10",      "24h", "am"},    // nist time colorado
    {15000000, "wwv 15",      "24h", "am"},    // nist time colorado
    {20000000, "wwv 20",      "24h", "am"},    // nist time colorado
    {3330000,  "chv",         "24h", "am"},    // canadian time
    {7850000,  "chv",         "24h", "am"},    // canadian time
    {14670000, "chv",         "24h", "am"},    // canadian time
    
    // bbc world service
    {3255000,  "bbc",         "night", "am"},  // south africa relay
    {5875000,  "bbc",         "night", "am"},  // ascension island
    {6195000,  "bbc",         "night", "am"},  // singapore relay
    {9410000,  "bbc",         "night", "am"},  // middle east
    {12095000, "bbc",         "day",   "am"},  // south asia
    {15400000, "bbc",         "day",   "am"},  // africa
    
    // voice of america
    {6080000,  "voa",         "night", "am"},  // africa
    {9885000,  "voa",         "night", "am"},  // middle east
    {15580000, "voa",         "day",   "am"},  // africa
    
    // radio havana cuba
    {6000000,  "rhc",         "night", "am"},  // english
    {6165000,  "rhc",         "night", "am"},  // english
    {11760000, "rhc",         "night", "am"},  // spanish
    
    // china radio international
    {9570000,  "cri",         "night", "am"},  // english
    {11710000, "cri",         "day",   "am"},  // english
    {13640000, "cri",         "day",   "am"},  // english
    
    // amateur radio bands (always active)
    {3750000,  "80m ssb",     "night", "lsb"},
    {7074000,  "40m ft8",     "24h",   "usb"},  // digital mode
    {7200000,  "40m ssb",     "night", "lsb"},
    {14074000, "20m ft8",     "day",   "usb"},  // digital mode
    {14230000, "20m ssb",     "day",   "usb"},
    {21074000, "15m ft8",     "day",   "usb"},  // digital mode
    {21200000, "15m ssb",     "day",   "usb"},
    {28074000, "10m ft8",     "day",   "usb"},  // digital mode
    {28400000, "10m ssb",     "day",   "usb"},
    
    // numbers stations (spy stuff)
    {4625000,  "uvb-76",      "24h",   "am"},  // the buzzer (russia)
    {8992000,  "hfgcs",       "24h",   "usb"}, // us military
    {11175000, "hfgcs",       "24h",   "usb"}, // us military
    
    // pirate radio (evenings/weekends)
    {6925000,  "pirate",      "night", "am"},  // north america
    {6930000,  "pirate",      "night", "am"},
    {6935000,  "pirate",      "night", "am"},
    
    // aviation
    {5680000,  "aviation",    "24h",   "usb"},
    {8891000,  "aviation",    "24h",   "usb"},
    {11336000, "aviation",    "24h",   "usb"},
    
    // weather fax
    {3357000,  "weather fax", "24h",   "usb"},
    {7795000,  "weather fax", "24h",   "usb"},
    {9982500,  "weather fax", "24h",   "usb"},
};

static const int NUM_STATIONS = sizeof(STATIONS) / sizeof(Station);

// quick access favorites
static const int FAVORITES[] = {
    1,   // wwv 5mhz - time signal
    13,  // bbc world service
    25,  // 40m amateur
    27,  // 20m amateur  
    31,  // uvb-76 buzzer
    35,  // pirate radio
};

// get station by index
inline const Station* getStation(int index) {
    if (index >= 0 && index < NUM_STATIONS) {
        return &STATIONS[index];
    }
    return nullptr;
}

// find nearest station to frequency
inline const Station* findNearestStation(float freq) {
    const Station* nearest = nullptr;
    float minDiff = 1000000.0f;  // 1mhz max diff
    
    for (int i = 0; i < NUM_STATIONS; i++) {
        float diff = fabsf(STATIONS[i].freq - freq);
        if (diff < minDiff) {
            minDiff = diff;
            nearest = &STATIONS[i];
        }
    }
    
    return nearest;
}