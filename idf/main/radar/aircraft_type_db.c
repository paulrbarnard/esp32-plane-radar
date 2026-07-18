#include "aircraft_type_db.h"

#include <string.h>

typedef struct {
    const char *designator;
    const char *manufacturer;
    const char *model;
    const char *airframe;
    const char *engine_type;
    uint8_t engine_count;
} type_record_t;

/* Compact, local ICAO designator catalogue. Additions are intentionally
 * data-only, so coverage can grow without changing UI or network behaviour. */
static const type_record_t type_db[] = {
    {"A109", "Leonardo", "AW109", "Helicopter", "Turboshaft", 2},
    {"A119", "Leonardo", "AW119 Koala", "Helicopter", "Turboshaft", 1},
    {"A139", "Leonardo", "AW139", "Helicopter", "Turboshaft", 2},
    {"A20N", "Airbus", "A320neo", "Landplane", "Turbofan", 2},
    {"A21N", "Airbus", "A321neo", "Landplane", "Turbofan", 2},
    {"A318", "Airbus", "A318", "Landplane", "Turbofan", 2},
    {"A319", "Airbus", "A319", "Landplane", "Turbofan", 2},
    {"A320", "Airbus", "A320", "Landplane", "Turbofan", 2},
    {"A321", "Airbus", "A321", "Landplane", "Turbofan", 2},
    {"A332", "Airbus", "A330-200", "Landplane", "Turbofan", 2},
    {"A333", "Airbus", "A330-300", "Landplane", "Turbofan", 2},
    {"A338", "Airbus", "A330-800neo", "Landplane", "Turbofan", 2},
    {"A339", "Airbus", "A330-900neo", "Landplane", "Turbofan", 2},
    {"A359", "Airbus", "A350-900", "Landplane", "Turbofan", 2},
    {"A35K", "Airbus", "A350-1000", "Landplane", "Turbofan", 2},
    {"A388", "Airbus", "A380-800", "Landplane", "Turbofan", 4},
    {"AS50", "Airbus Helicopters", "AS350 Ecureuil", "Helicopter", "Turboshaft", 1},
    {"AS55", "Airbus Helicopters", "AS355 Ecureuil", "Helicopter", "Turboshaft", 2},
    {"AT43", "ATR", "ATR 42", "Landplane", "Turboprop", 2},
    {"AT45", "ATR", "ATR 42-500", "Landplane", "Turboprop", 2},
    {"AT46", "ATR", "ATR 42-600", "Landplane", "Turboprop", 2},
    {"AT72", "ATR", "ATR 72", "Landplane", "Turboprop", 2},
    {"B06", "Bell", "206 JetRanger", "Helicopter", "Turboshaft", 1},
    {"B190", "Beechcraft", "1900", "Landplane", "Turboprop", 2},
    {"B350", "Beechcraft", "King Air 350", "Landplane", "Turboprop", 2},
    {"B38M", "Boeing", "737 MAX 8", "Landplane", "Turbofan", 2},
    {"B39M", "Boeing", "737 MAX 9", "Landplane", "Turbofan", 2},
    {"B3XM", "Boeing", "737 MAX", "Landplane", "Turbofan", 2},
    {"B733", "Boeing", "737-300", "Landplane", "Turbofan", 2},
    {"B734", "Boeing", "737-400", "Landplane", "Turbofan", 2},
    {"B735", "Boeing", "737-500", "Landplane", "Turbofan", 2},
    {"B736", "Boeing", "737-600", "Landplane", "Turbofan", 2},
    {"B737", "Boeing", "737-700", "Landplane", "Turbofan", 2},
    {"B738", "Boeing", "737-800", "Landplane", "Turbofan", 2},
    {"B739", "Boeing", "737-900", "Landplane", "Turbofan", 2},
    {"B744", "Boeing", "747-400", "Landplane", "Turbofan", 4},
    {"B748", "Boeing", "747-8", "Landplane", "Turbofan", 4},
    {"B752", "Boeing", "757-200", "Landplane", "Turbofan", 2},
    {"B753", "Boeing", "757-300", "Landplane", "Turbofan", 2},
    {"B762", "Boeing", "767-200", "Landplane", "Turbofan", 2},
    {"B763", "Boeing", "767-300", "Landplane", "Turbofan", 2},
    {"B772", "Boeing", "777-200", "Landplane", "Turbofan", 2},
    {"B77L", "Boeing", "777 Freighter", "Landplane", "Turbofan", 2},
    {"B77W", "Boeing", "777-300ER", "Landplane", "Turbofan", 2},
    {"B788", "Boeing", "787-8", "Landplane", "Turbofan", 2},
    {"B789", "Boeing", "787-9", "Landplane", "Turbofan", 2},
    {"BE20", "Beechcraft", "King Air 200", "Landplane", "Turboprop", 2},
    {"BE36", "Beechcraft", "Bonanza 36", "Landplane", "Piston", 1},
    {"BE40", "Beechcraft", "Hawker 400", "Landplane", "Turbofan", 2},
    {"BE58", "Beechcraft", "Baron 58", "Landplane", "Piston", 2},
    {"C152", "Cessna", "152", "Landplane", "Piston", 1},
    {"C172", "Cessna", "172 Skyhawk", "Landplane", "Piston", 1},
    {"C182", "Cessna", "182 Skylane", "Landplane", "Piston", 1},
    {"C206", "Cessna", "206 Stationair", "Landplane", "Piston", 1},
    {"C208", "Cessna", "208 Caravan", "Landplane", "Turboprop", 1},
    {"C210", "Cessna", "210 Centurion", "Landplane", "Piston", 1},
    {"C25A", "Cessna", "Citation CJ2", "Landplane", "Turbofan", 2},
    {"C525", "Cessna", "CitationJet", "Landplane", "Turbofan", 2},
    {"C550", "Cessna", "Citation II", "Landplane", "Turbofan", 2},
    {"C560", "Cessna", "Citation V", "Landplane", "Turbofan", 2},
    {"C650", "Cessna", "Citation III", "Landplane", "Turbofan", 2},
    {"C680", "Cessna", "Citation Sovereign", "Landplane", "Turbofan", 2},
    {"C750", "Cessna", "Citation X", "Landplane", "Turbofan", 2},
    {"CL30", "Bombardier", "Challenger 300", "Landplane", "Turbofan", 2},
    {"CRJ1", "Bombardier", "CRJ100", "Landplane", "Turbofan", 2},
    {"CRJ2", "Bombardier", "CRJ200", "Landplane", "Turbofan", 2},
    {"CRJ7", "Bombardier", "CRJ700", "Landplane", "Turbofan", 2},
    {"CRJ9", "Bombardier", "CRJ900", "Landplane", "Turbofan", 2},
    {"D328", "Dornier", "328", "Landplane", "Turboprop", 2},
    {"DH8A", "De Havilland Canada", "Dash 8-100", "Landplane", "Turboprop", 2},
    {"DH8B", "De Havilland Canada", "Dash 8-200", "Landplane", "Turboprop", 2},
    {"DH8C", "De Havilland Canada", "Dash 8-300", "Landplane", "Turboprop", 2},
    {"DH8D", "De Havilland Canada", "Dash 8-400", "Landplane", "Turboprop", 2},
    {"E135", "Embraer", "ERJ-135", "Landplane", "Turbofan", 2},
    {"E145", "Embraer", "ERJ-145", "Landplane", "Turbofan", 2},
    {"E170", "Embraer", "E170", "Landplane", "Turbofan", 2},
    {"E175", "Embraer", "E175", "Landplane", "Turbofan", 2},
    {"E190", "Embraer", "E190", "Landplane", "Turbofan", 2},
    {"E195", "Embraer", "E195", "Landplane", "Turbofan", 2},
    {"E290", "Embraer", "E190-E2", "Landplane", "Turbofan", 2},
    {"E295", "Embraer", "E195-E2", "Landplane", "Turbofan", 2},
    {"EC35", "Airbus Helicopters", "H135", "Helicopter", "Turboshaft", 2},
    {"F2TH", "Dassault", "Falcon 2000", "Landplane", "Turbofan", 2},
    {"F900", "Dassault", "Falcon 900", "Landplane", "Turbofan", 3},
    {"GL5T", "Gulfstream", "G500", "Landplane", "Turbofan", 2},
    {"GL7T", "Gulfstream", "G700", "Landplane", "Turbofan", 2},
    {"H135", "Airbus Helicopters", "H135", "Helicopter", "Turboshaft", 2},
    {"H145", "Airbus Helicopters", "H145", "Helicopter", "Turboshaft", 2},
    {"LJ35", "Learjet", "35", "Landplane", "Turbofan", 2},
    {"PA28", "Piper", "PA-28 Cherokee", "Landplane", "Piston", 1},
    {"PA32", "Piper", "PA-32 Saratoga", "Landplane", "Piston", 1},
    {"PA34", "Piper", "PA-34 Seneca", "Landplane", "Piston", 2},
    {"PA44", "Piper", "PA-44 Seminole", "Landplane", "Piston", 2},
    {"PC12", "Pilatus", "PC-12", "Landplane", "Turboprop", 1},
    {"PC24", "Pilatus", "PC-24", "Landplane", "Turbofan", 2},
    {"R22", "Robinson", "R22", "Helicopter", "Piston", 1},
    {"R44", "Robinson", "R44", "Helicopter", "Piston", 1},
    {"S76", "Sikorsky", "S-76", "Helicopter", "Turboshaft", 2},
    {"S92", "Sikorsky", "S-92", "Helicopter", "Turboshaft", 2},
    {"SR20", "Cirrus", "SR20", "Landplane", "Piston", 1},
    {"SR22", "Cirrus", "SR22", "Landplane", "Piston", 1},
};

bool radar_type_lookup(const char *designator, radar_type_info_t *info)
{
    if (!designator || !designator[0] || !info) return false;
    for (size_t i = 0; i < sizeof(type_db) / sizeof(type_db[0]); ++i) {
        if (strcmp(designator, type_db[i].designator) == 0) {
            info->manufacturer = type_db[i].manufacturer;
            info->model = type_db[i].model;
            info->airframe = type_db[i].airframe;
            info->engine_type = type_db[i].engine_type;
            info->engine_count = type_db[i].engine_count;
            return true;
        }
    }
    return false;
}
