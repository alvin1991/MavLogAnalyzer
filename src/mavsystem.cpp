/**
 * @file mavsystem.cpp
 * @brief Holds all information that has been extracted about one system.
 * @author Martin Becker <becker@rcs.ei.tum.de>
 * @date 18.04.2014
 
    This file is part of MavLogAnalyzer, Copyright 2014 by Martin Becker.
    
    MavLogAnalyzer is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    
 */

#include "config.h"
#include <iostream>
#include <cassert>
#include <sstream>
#include <string>
#include <time.h>
#include <math.h>
#include "mavsystem.h"
#include "mavlink.h"
#include "stringfun.h"
#include "datagroup.h"
#include "data_event.h"
#include "data_timeseries.h"
#include "data_param.h"
#include "treeitem.h"
#include "time_fun.h"
#include "mavsystem_macros.h"
#include "logger.h"

using namespace std;

/*********************************************
 *      DEFINITIONS
 *********************************************/

#define RAD2DEG(x) x*(180./M_PI)
#define DEG2RAD(x) (x*M_PI/180.)

inline double angle360 (double x) {
    if (isnan(x) || isinf(x)) return x;
    while (x >= 360.) x-=360.;
    while (x < 0.) x += 360.;
    return x;
}

/*********************************************
 *      CLASS FUNCTIONS
 *********************************************/

void MavSystem::_defaults() {    
    mavtype_str = "unknown";
    aptype_str = "unknown";
    mavtype = MAVTYPE_INIT;
    aptype = MAVAPTYPE_INIT;
    has_been_armed = false;
    _time_max = -INFINITY;
    _time_min = INFINITY;
    _time_valid = false;
    _time_offset_guess_usec = 0;
    _time_maxfwdjump_sec = 100.;
    _time_maxbackjump_sec = 5.;
    _have_time_update = false;
    _mavlink_summary._link_throughput_bytes = 0;
    _mavlink_summary.num_uninterpreted = 0;
    _mavlink_summary.num_received = 0;
    _mavlink_summary.num_interpreted = 0;
    _mavlink_summary.num_error = 0;

    stringstream logname;
    logname << "log_mavsystem_" << id;
    _logchannel = Logger::Instance().createChannel(logname.str());
    deferredLoad = false;        
}

string MavSystem::_aptype2str(uint8_t atype) {
    switch (atype) {
    case MAV_AUTOPILOT_GENERIC:
        return "generic";
    //case MAV_AUTOPILOT_PIXHAWK:
    //        return "Pixhawk";
    case MAV_AUTOPILOT_SLUGS:
        return "Slugs";
    case MAV_AUTOPILOT_ARDUPILOTMEGA:
        return "ArduPilotMega";
    case MAV_AUTOPILOT_OPENPILOT:
        return "OpenPilot";
    case MAV_AUTOPILOT_PX4:
        return "PX4";
    default:
        return "unknown";
    }
}

string MavSystem::_mavtype2str(uint8_t mtype) {
    switch (mtype) {
     case	MAV_TYPE_GENERIC:
         return string("generic");
     case	MAV_TYPE_FIXED_WING:
         return string("fixed wing");
     case	MAV_TYPE_QUADROTOR:
         return string("quadrotor");
     case	MAV_TYPE_COAXIAL:
         return string("coax");
     case	MAV_TYPE_HELICOPTER:
         return string("heli");
     case	MAV_TYPE_ANTENNA_TRACKER:
         return string("antennatracker");
     case	MAV_TYPE_GCS:
         return string("GCS");
     case	MAV_TYPE_AIRSHIP:
         return string("airship");
     case	MAV_TYPE_FREE_BALLOON:
         return string("balloon");
     case	MAV_TYPE_ROCKET:
         return string("rocket");
     case	MAV_TYPE_GROUND_ROVER:
         return string("rover");
     case	MAV_TYPE_SURFACE_BOAT:
         return string("boat");
     case	MAV_TYPE_SUBMARINE:
         return string("submarine");
     case	MAV_TYPE_HEXAROTOR:
         return string("hexarotor");
     case	MAV_TYPE_OCTOROTOR:
         return string("octarotor");
     case	MAV_TYPE_TRICOPTER:
         return string("tricopter");
     case	MAV_TYPE_FLAPPING_WING:
         return string("flapwing");
     case	MAV_TYPE_KITE:
         return string("kite");
     case	MAV_TYPE_ONBOARD_CONTROLLER:
         return string("onboard controller");
     default:
         return string("unknown");
    }
}

void MavSystem::get_summary(string &buf) const {
    stringstream ss;

    /***************************************/
    ss << "General:" << endl <<
    /***************************************/
          "   - id: " << id << endl <<
          "   - type: " << mavtype_str << endl <<
          "   - autopilot: " << aptype_str << endl <<
          "   - has_been_armed: " << has_been_armed << endl <<
          "   - active for " << seconds_to_timestr(get_time_active_end() - get_time_active_begin()) <<
                " beween " << epoch_to_datetime(get_time_active_begin()) << " and " <<
                              epoch_to_datetime(get_time_active_end()) << endl;

    if (!deferredLoad) {
        /***************************************/
        ss << "Power:" << endl;
        /***************************************/
        MAVSYSTEM_READ_DATA(DataTimeseries<float>, bat_volt, "power/battery_voltage");
        if (bat_volt) { ss << "   - battery voltage: " << bat_volt->get_max() << " ... " << bat_volt->get_min() << " " << bat_volt->get_units() << endl; }
        MAVSYSTEM_READ_DATA(DataTimeseries<float>, bat_amps, "power/battery_current");
        if (bat_amps) { ss << "   - battery current: " << bat_amps->get_min() << " ... " << bat_amps->get_max() << " " << bat_amps->get_units() << endl; }

        /***************************************/
        ss << "Flight Book:" << endl;
        /***************************************/
        MAVSYSTEM_READ_DATA(DataParam<double>, first_takeoff, "flightbook/first takeoff");
        if (first_takeoff) { ss << "   - first takeoff: " << epoch_to_datetime(first_takeoff->get_value() + (first_takeoff->get_epoch_datastart()/1E6)) << endl; }
        MAVSYSTEM_READ_DATA(DataParam<double>, last_landing, "flightbook/last landing");
        if (last_landing) { ss << "   - last landing: " << epoch_to_datetime(last_landing->get_value() + (last_landing->get_epoch_datastart()/1E6)) << endl; }
        MAVSYSTEM_READ_DATA(DataParam<unsigned int>, nflights, "flightbook/number flights");
        if (nflights) { ss << "   - number of flights: " << nflights->get_value() << endl; }
        MAVSYSTEM_READ_DATA(DataParam<double>, flighttime, "flightbook/total flight time");
        if (flighttime) { ss << "   - total flight time: " << seconds_to_timestr(flighttime->get_value(), false) << endl; }

        /***************************************/
        ss << "Flight performance:" << endl;
        /***************************************/
        MAVSYSTEM_READ_DATA(DataTimeseries<float>, airspeed, "airstate/airspeed");
        if (airspeed) { ss << "   - airspeed: " << airspeed->get_min() << " ... " << airspeed->get_max() << " " << airspeed->get_units() << endl; }
        MAVSYSTEM_READ_DATA(DataTimeseries<float>, altmsl, "airstate/alt MSL");
        if (altmsl)   { ss << "   - alt. MSL: " << altmsl->get_min() << " ... " << altmsl->get_max() << " " << altmsl->get_units() << endl; }
        MAVSYSTEM_READ_DATA(DataTimeseries<float>, climb, "airstate/climb");
        if (climb)    { ss << "   - climb rate: " << climb->get_min() << " ... " << climb->get_max() << " " << climb->get_units() << endl; }
        MAVSYSTEM_READ_DATA(DataTimeseries<float>, throttle, "airstate/throttle");
        if (throttle) { ss << "   - throttle: " << throttle->get_min() << " ... " << throttle->get_max() << " " << throttle->get_units() << endl; }

        /***************************************/
        ss << "Last Position:" << endl;
        /***************************************/
        MAVSYSTEM_READ_DATA(DataTimeseries<double>, lat, "airstate/lat");
        if (lat)   { ss << "   - lat: " << lat->get_last().second << " " << lat->get_units() << endl; }
        MAVSYSTEM_READ_DATA(DataTimeseries<double>, lon, "airstate/lon");
        if (lon)   { ss << "   - lon: " << lon->get_last().second << " " << lon->get_units() << endl; }
        MAVSYSTEM_READ_DATA(DataTimeseries<float>, alt, "airstate/alt GND");
        if (alt)   { ss << "   - re. alt: " << alt->get_last().second << " " << alt->get_units() << endl; }

        /***************************************/
        ss << "Computer:" << endl;
        /***************************************/
        MAVSYSTEM_READ_DATA(DataTimeseries<float>, ap_load, "computer/autopilot_load");
        if (ap_load) { ss << "   - max. autopilot load: " << ap_load->get_max() << " " << ap_load->get_units() << endl; }

        /***************************************/
        ss << "MavLink:" << endl;
        /***************************************/
        ss << "   - sent total: " << _mavlink_summary.num_received << " (IDs: " << set2str(_mavlink_summary.mavlink_msgids_interpreted) << ")" << endl;
        if (_mavlink_summary.num_uninterpreted > 0) {
            ss << "   - uninterpreted: " << _mavlink_summary.num_uninterpreted << " (IDs: " << set2str(_mavlink_summary.mavlink_msgids_uninterpreted) << ")" << endl;
        }
        ss << "   - errors: "  << _mavlink_summary.num_error << endl;
    }

    buf = ss.str();
}

void MavSystem::_log(logmsgtype_e t, const std::string & str) {
    Logger::Instance().write(t, str, _logchannel);
}

/**
 * @brief hook a data item into a given hierarchy.
 * @param fullpath
 * @param item
 */
void MavSystem::_data_register_hierarchy(const string &fullname, Data *item) {

    string fullpath = fullname;
    fullpath = string_trim(fullpath);
    _data_from_path[fullname] = item; // will add if it doesn't exist yet

    _log(MSG_INFO, stringbuilder() << " Data: " << fullpath);

    // split path into levels
    vector<string> path;
    string_split(fullpath, '/', path);
    path.pop_back(); // remove basename

    /*****************************************^^*********
     *  Walk through hierarchy and create if necessary
     **************************************************/
    DataGroup*parentgroup=NULL;
    DataGroup*curgroup=NULL;
    DataGroup::groupmap*currentGroupMap = &mav_data_groups; // start at top level
    for (vector<string>::const_iterator itppath = path.begin(); itppath != path.end(); ++itppath) { // walk through path
        // see if it exists
        DataGroup::groupmap::iterator itgroup = currentGroupMap->find(*itppath);
        if (itgroup == currentGroupMap->end()) {
            // does not exist. create new group and save its ptr
            curgroup = new DataGroup(*itppath); // create group
            curgroup->parent = parentgroup;
            currentGroupMap->insert(currentGroupMap->begin(), DataGroup::groupmap_pair(*itppath, curgroup)); // insert into group list
        } else {
            curgroup = itgroup->second;
        }
        currentGroupMap = &curgroup->groups; // next level
        parentgroup = curgroup;
    }
    // finally...when we are here the path exists. All we have to do is hook in the data
    if (!curgroup) return;
    curgroup->data[item->get_name()] = item;
    item->parent = curgroup; // this line does not permit multi-parent...need to do it for TreeView widget. Trolltech, what are you doing with that TreeView??
}

void MavSystem::_data_cleanup() {
    // delete all data in _memory_data and delete groups
    for (data_accessmap::iterator it = _data_from_path.begin(); it != _data_from_path.end(); ++it) {
        delete it->second;
    }
    _data_from_path.clear();
    mav_data_groups.clear();
}

MavSystem::MavSystem(unsigned int sysid) : id(sysid), mavtype_str("unknown"), aptype_str("unknown"), _time(0.), _time_offset_usec(0) {
    _defaults();
}


/**
 * @brief hook a data item into a given hierarchy.
 * @param fullpath
 * @param item
 */
void MavSystem::_data_unregister_hierarchy(Data*const src) {
    // remove data from parent

    DataGroup*parentgroup = src->get_parent();
    if (!parentgroup) return; // now that should never happen. every data should have a parent.

    DataGroup::datamap::iterator it = parentgroup->data.find(src->get_name());
    if (it == parentgroup->data.end()) return; // notfound...weird
    parentgroup->data.erase(it);

    // clean hierarchy
    DataGroup*curgroup = parentgroup;
    while (curgroup) {
        parentgroup = curgroup->parent;
        // see if group would be empty after removal
        bool groupempty = (curgroup->groups.empty() && curgroup->data.empty());
        if (groupempty) {
            if (parentgroup) {  // parent is another group -> clean parent
                DataGroup::groupmap::iterator it = parentgroup->groups.find(curgroup->groupname);
                if (it != parentgroup->groups.end()) parentgroup->groups.erase(it);
            } else {            // parent is MavSystem -> clean mavsystem
                DataGroup::groupmap::iterator it = this->mav_data_groups.find(curgroup->groupname);
                if (it != this->mav_data_groups.end()) this->mav_data_groups.erase(it);
            }
            // delete group itself
            delete curgroup;
        }
        curgroup = parentgroup;
    }
}

void MavSystem::_del_data(Data*const src) {
    if (!src) return;
    const string fullpath = Data::get_fullname(src);

    // remove from map
    data_accessmap::iterator it1 = _data_from_path.find(fullpath);
    if (it1 != _data_from_path.end()) _data_from_path.erase(it1);

    // remove from hierarchy
    _data_unregister_hierarchy(src);

    // delete the data itself
    delete src; // FIXME: deleting abstract class here...
}

bool MavSystem::_add_data(const Data*const src) {
    // find data path and register to get it into the hierarchy
    if (!src) return false;

    string fullname = Data::get_fullname(src);

    // check whether data exists already
    Data * mydata = _get_data<Data>(fullname);
    if (mydata) {
        // could still be empty
        if (mydata->is_present()) {
            // already exists -> ask data class to merge it in
            return mydata->merge_in(src);
        } else {
            _del_data(mydata); // drop old, empty data!!
            // since there is nothing now, clone it
            Data*const copiedData = src->Clone(); ///< call copy CTOR (covariant return)
            _data_register_hierarchy(fullname, copiedData);
            if (!copiedData) return false;
            return true;
        }
    } else {
        // does not exist -> take a deep copy and register it
        Data*const copiedData = src->Clone(); ///< call copy CTOR (covariant return)
        if (!copiedData) return false;
        _data_register_hierarchy(fullname, copiedData);
        return true;
    }
}

// DONE
MavSystem::MavSystem(const MavSystem *other) {
    _defaults(); ///< just to be sure...there must be no uninitialized data
    id = other->id;
    mavtype = other->mavtype;
    mavtype_str = other->mavtype_str;
    aptype = other->aptype;
    aptype_str = other->aptype_str;
    has_been_armed = other->has_been_armed;
    _time = other->_time;
    _time_min = other->_time_min;
    _time_max = other->_time_max;
    _time_offset_raw = other->_time_offset_raw;
    _time_offset_usec = other->_time_offset_usec;
    _time_offset_guess_usec = other->_time_offset_guess_usec;
    _time_valid = other->_time_valid;
    _mavlink_summary = other->_mavlink_summary;

    // copy data inside, the datagroup is not copied but created with our own functions again
    for (data_accessmap::const_iterator ito = other->_data_from_path.begin(); ito != other->_data_from_path.end(); ++ito) {
        // copy each item by copying *Data inside
        const Data*const data = ito->second;
        _add_data(data);
    }
}

MavSystem::~MavSystem() {
    _data_cleanup();
    Logger::Instance().deleteChannel(_logchannel);
}

void MavSystem::track_system(uint8_t stype, uint8_t status, uint8_t atype, uint8_t basemode, uint8_t custmode) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataEvent<string>, evt_armed, "mission/armed", "");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataEvent<string>, evt_stabilized, "mission/stabilized", "");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataEvent<string>, evt_guided, "mission/guided", "");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataEvent<string>, evt_manual, "mission/manual", "");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataEvent<string>, evt_status, "system/status", "MAV_STATE_ENUM");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_custmode, "system/custom_mode", "autopilot-specific mode");

    data_custmode->add_elem(custmode, _time);

    /*************************
     *  OVERALL SYSTEM STATUS
     *************************/
    {
        string strstate;
        switch (status) {
        case MAV_STATE_UNINIT:
            strstate = "uninitialized";
            break;
        case MAV_STATE_BOOT:
            strstate = "boot";
            break;
        case MAV_STATE_CALIBRATING:
            strstate = "calibrating";
            break;
        case MAV_STATE_STANDBY:
            strstate = "standby";
            break;
        case MAV_STATE_ACTIVE:
            strstate = "active";
            break;
        case MAV_STATE_CRITICAL:
            strstate = "critical";
            break;
        case MAV_STATE_EMERGENCY:
            strstate = "emergency";
            break;
        case MAV_STATE_POWEROFF:
            strstate = "poweroff";
            break;
        default:
            strstate = "unknown";
        }
        if (evt_status->size()) {
            if (evt_status->get_latest().compare(strstate)) {
                evt_status->add_elem(strstate, _time);
            }
        } else {
            evt_status->add_elem(strstate, _time);
        }
    }


    /*********************
     *  ARMED STATE
     *********************/
    {
        const bool is = (basemode & MAV_MODE_FLAG_SAFETY_ARMED);
        string str = is ? "armed" : "disarmed";
        if (evt_armed->size()) {
            if (evt_armed->get_latest().compare(str)) {
                evt_armed->add_elem(str, _time);
            }
        } else { evt_armed->add_elem(str, _time); }
        if (is) { has_been_armed = true; }
    }

    /*********************
     *  STABILIZED STATE
     *********************/
    {
        const bool is = (basemode & MAV_MODE_FLAG_STABILIZE_ENABLED);
        string str = is ? "stabilized on" : "stabilized off";
        if (evt_stabilized->size()) {
            if (evt_stabilized->get_latest().compare(str)) {
                evt_stabilized->add_elem(str, _time);
            }
        } else { evt_stabilized->add_elem(str, _time); };
    }

    /*********************
     *  GUIDED STATE
     *********************/
    {
        const bool is = (basemode & MAV_MODE_FLAG_GUIDED_ENABLED);
        string str = is ? "guided on" : "guided off";
        if (evt_guided->size()) {
            if (evt_guided->get_latest().compare(str)) {
                evt_guided->add_elem(str, _time);
            }
        } else { evt_guided->add_elem(str, _time);}
    }

    /*********************
     *  MANUAL STATE
     *********************/
    {
        const bool is = (basemode & MAV_MODE_FLAG_MANUAL_INPUT_ENABLED);
        string str = is ? "manual on" : "manual off";
        if (evt_manual->size()) {
            if (evt_manual->get_latest().compare(str)) {
                evt_manual->add_elem(str, _time);
            }
        } else { evt_manual->add_elem(str, _time); }
    }

    /*********************
     *  TYPE
     *********************/
    if (mavtype != stype) {
        if (MAVTYPE_INIT != mavtype) {
            _log(MSG_WARN, stringbuilder() << "WARNING: MAV id=" << id << " changes type from " << mavtype << " to " << stype);
        }
        mavtype = stype;
        mavtype_str = _mavtype2str(stype);
    }

    /*********************
     *  AUTOPILOT TYPE
     *********************/
    if (aptype != atype) {
        if (MAVTYPE_INIT != mavtype) {
            _log(MSG_WARN, stringbuilder() << "WARNING: MAV id=" << id << " changes autopilot from " << aptype << " to " << atype);
        }
        aptype = atype;
        aptype_str = _aptype2str(atype);
    }
}

/**
 * @brief keep track of system data
 * @param load
 * @param bat_V
 * @param bat_A
 */
void MavSystem::track_sysperf(float load, float bat_V, float bat_A) {        
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_autopilot_load, "computer/autopilot_load", "%");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_battery_volt,   "power/battery_voltage", "V");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_battery_amps,   "power/battery_current", "A");

    data_autopilot_load->add_elem(load, _time);
    if (bat_A > 0.) data_battery_amps->add_elem(bat_A, _time);
    if (bat_V > 0.) data_battery_volt->add_elem(bat_V, _time);
}

/**
 * @brief keep track of ambient/environmental
 * @param temp_degC
 * @param press_hPa
 */
void MavSystem::track_ambient(float temp_degC, float press_hPa) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_temp, "environment/temperature", "deg C");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_press, "environment/static pressure", "hPa");

    data_press->add_elem(press_hPa, _time);
    data_temp->add_elem(temp_degC, _time);
}

/**
 * @brief keep track of how much data was sent
 * @param data_length length of an incoming packet, inkl. header
 * @param msgid MAVlink message identifier
 * @param whatwasdone to indicate how the caller processed the message; useful to see if MavLogAnalyzer misses messages
 */
void MavSystem::track_mavlink(unsigned int data_length_bytes, unsigned int msgid, mavlink_parsed_e whatwasdone) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_throughput, "radio/throughput", "kbps");

    // accumulate amount of sent data between two successive time references
    _mavlink_summary._link_throughput_bytes += data_length_bytes;

    // track basic information what was done with the packet
    switch (whatwasdone) {
    case MAVLINK_INTERPRETED:
        _mavlink_summary.num_interpreted++;
        _mavlink_summary.mavlink_msgids_interpreted.insert(msgid);
        break;
    case MAVLINK_UNINTERPRETED:
        _mavlink_summary.num_uninterpreted++;
        _mavlink_summary.mavlink_msgids_uninterpreted.insert(msgid);
        break;
    case MAVLINK_ERROR:
    default:
        _mavlink_summary.num_error++;
        break;
    }
    _mavlink_summary.num_received++;

    // only add to series if time information is available
    if (_have_time_update) {
        data_throughput->add_elem(_mavlink_summary._link_throughput_bytes/128., _time);
        _mavlink_summary._link_throughput_bytes = 0;
    }
}

/**
 * @brief keep track of flight data except location
 * @param airspeed_ms
 * @param groundspeed_ms
 * @param alt_MSL_m
 * @param climb_ms
 * @param throttle_percent
 */
void MavSystem::track_flightperf(float airspeed_ms, float groundspeed_ms, float /*alt_MSL_m*/, float climb_ms, float throttle_percent) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_airspeed_ms, "airstate/airspeed", "m/s");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_groundspeed_ms, "airstate/groundspeed", "m/s");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_alt_MSL_m, "airstate/alt MSL", "m");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_climb_ms, "airstate/climb", "m/s");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_throttle_percent, "airstate/throttle", "%");

    data_airspeed_ms->add_elem(airspeed_ms, _time);
    data_groundspeed_ms->add_elem(groundspeed_ms, _time);
    //data_alt_MSL_m->add_elem(alt_MSL_m, _time); ///< there is a problem with that guy...it switches between GND and MSL
    data_climb_ms->add_elem(climb_ms, _time);
    data_throttle_percent->add_elem(throttle_percent, _time);
}

/**
 * @brief keep track of flight path
 * @param lat
 * @param lon
 * @param alt_rel_m
 */
void MavSystem::track_paths(double lat, double lon, float alt_rel_m, float alt_msl_m, float heading_deg) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<double>, data_lat, "airstate/lat", "");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<double>, data_lon, "airstate/lon", "");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_alt_GND, "airstate/alt GND", "m");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_alt_MSL, "airstate/alt MSL", "m");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_heading, "airstate/heading", "deg");

    data_lat->add_elem(lat, _time);
    data_lon->add_elem(lon, _time);
    data_alt_GND->add_elem(alt_rel_m, _time);
    data_alt_MSL->add_elem(alt_msl_m, _time);
    if (heading_deg <= 360.f) {
        data_heading->add_elem(heading_deg, _time);
    }
}

void MavSystem::track_paths_attitude(const float rpy[], const float speed_rpy[]) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, roll, "airstate/angles/roll", "deg");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, pitch, "airstate/angles/pitch", "deg");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, yaw, "airstate/angles/yaw", "deg");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, omg_x, "airstate/rate/roll rate", "deg/s");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, omg_y, "airstate/rate/pitch rate", "deg/s");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, omg_z, "airstate/rate/yaw rate", "deg/s");

    roll->add_elem(RAD2DEG(rpy[0]), _time);
    pitch->add_elem(RAD2DEG(rpy[1]), _time);
    yaw->add_elem(RAD2DEG(rpy[2]), _time);
    omg_x->add_elem(RAD2DEG(speed_rpy[0]), _time);
    omg_y->add_elem(RAD2DEG(speed_rpy[1]), _time);
    omg_z->add_elem(RAD2DEG(speed_rpy[2]), _time);
}

void MavSystem::track_mission_current(uint16_t seq) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_current, "mission/current seq", "item id");
    data_current->add_elem(seq, _time);
}

void MavSystem::track_mission_item(uint8_t target_system_id, uint8_t target_comp_id, uint16_t seq, uint8_t frame, uint16_t command, uint8_t current, uint8_t autocontinue, float param1, float param2, float param3, float param4, float x, float y, float z) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_sysid,  "mission/target system id", "item id");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_compid, "mission/component id", "item id");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_seq,    "mission/seq", "item id");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_frame,  "mission/frame", "MAV_FRAME enum");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_command, "mission/command", "MAV_CMD enum");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_current, "mission/current", "bool");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_autocont, "mission/autocontinue", "");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_param1, "mission/param1", "MAV_CMD enum");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_param2, "mission/param2", "MAV_CMD enum");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_param3, "mission/param3", "MAV_CMD enum");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_param4, "mission/param4", "MAV_CMD enum");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_x, "mission/x", "local: x pos. global: latitude");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_y, "mission/y", "local: y pos. global: longitude");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_z, "mission/z", "local: z pos. global: alt (rel. or abs.)");

    data_sysid->add_elem(target_system_id, _time);
    data_compid->add_elem(target_comp_id, _time);
    data_seq->add_elem(seq, _time);
    data_frame->add_elem(frame, _time);
    data_command->add_elem(command, _time);
    data_current->add_elem(current, _time);
    data_autocont->add_elem(autocontinue, _time);
    data_param1->add_elem(param1, _time);
    data_param2->add_elem(param2, _time);
    data_param3->add_elem(param3, _time);
    data_param4->add_elem(param4, _time);
    data_x->add_elem(x, _time);
    data_y->add_elem(y, _time);
    data_z->add_elem(z, _time);
}

void MavSystem::track_rc(const uint16_t channels[8]) {
    DataTimeseries<unsigned int>*const data_chan[] = {
        MAVSYSTEM_DATA_ITEM_HAVEVAR(DataTimeseries<unsigned int>, "rc/channel_1", "us"),
        MAVSYSTEM_DATA_ITEM_HAVEVAR(DataTimeseries<unsigned int>, "rc/channel_2", "us"),
        MAVSYSTEM_DATA_ITEM_HAVEVAR(DataTimeseries<unsigned int>, "rc/channel_3", "us"),
        MAVSYSTEM_DATA_ITEM_HAVEVAR(DataTimeseries<unsigned int>, "rc/channel_4", "us"),
        MAVSYSTEM_DATA_ITEM_HAVEVAR(DataTimeseries<unsigned int>, "rc/channel_5", "us"),
        MAVSYSTEM_DATA_ITEM_HAVEVAR(DataTimeseries<unsigned int>, "rc/channel_6", "us"),
        MAVSYSTEM_DATA_ITEM_HAVEVAR(DataTimeseries<unsigned int>, "rc/channel_7", "us"),
        MAVSYSTEM_DATA_ITEM_HAVEVAR(DataTimeseries<unsigned int>, "rc/channel_8", "us")
    };
    for (unsigned int k=0; k<8; k++) {
        if (data_chan[k]) data_chan[k]->add_elem(channels[k], _time);
    }
}

void MavSystem::track_paths_speed(const float v[]) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_vx, "airstate/speed/vx", "m/s");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_vy, "airstate/speed/vy", "m/s");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_vz, "airstate/speed/vz", "m/s");

    data_vx->add_elem(v[0], _time);
    data_vy->add_elem(v[1], _time);
    data_vz->add_elem(v[2], _time);
}

void MavSystem::track_gps_status(double lat, double lon, float alt_wgs, float hdop, float vdop,
                                 float vel_ms, float groundcourse) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<double>, data_lat, "GPS/lat", "");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<double>, data_lon, "GPS/lon", "");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_alt, "GPS/alt WGS84", "m");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_hdop, "GPS/hdop", "m");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_vdop, "GPS/vdop", "m");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_velo, "GPS/ground speed", "m/s");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_cog, "GPS/ground course", "deg");

    data_lat->add_elem(lat, _time);
    data_lon->add_elem(lon, _time);
    data_alt->add_elem(alt_wgs, _time);
    data_hdop->add_elem(hdop, _time);
    data_vdop->add_elem(vdop, _time);
    data_velo->add_elem(vel_ms, _time);
    data_cog->add_elem(groundcourse, _time);
}

/**
 * @brief keep track of the GPS sensor
 * @param n_sat
 * @param fix_type
 */
void MavSystem::track_gps_status(uint8_t n_sat, uint8_t fix_type) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, gps_sat, "GPS/num sat", "");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, gps_fix, "GPS/fix type", "");
    gps_sat->add_elem(n_sat, _time);

    if (fix_type < 255) {
        gps_fix->add_elem(fix_type, _time);
    }
}

void MavSystem::track_imu2(const int16_t acc_mg[], const int16_t gyr_mrs[], const int16_t mag_mT[]) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, xacc_mg, "IMU2/acc/acc x", "g");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, yacc_mg, "IMU2/acc/acc y", "g");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, zacc_mg, "IMU2/acc/acc z", "g");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, xgyr_mrs, "IMU2/gyro/omg x", "rad/s");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, ygyr_mrs, "IMU2/gyro/omg y", "rad/s");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, zgyr_mrs, "IMU2/gyro/omg z", "rad/s");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, xmag_mT, "IMU2/magnetic/mag x", "T");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, ymag_mT, "IMU2/magnetic/mag y", "T");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, zmag_mT, "IMU2/magnetic/mag z", "T");

    xacc_mg->add_elem(acc_mg[0]/1000.f, _time);
    yacc_mg->add_elem(acc_mg[1]/1000.f, _time);
    zacc_mg->add_elem(acc_mg[2]/1000.f, _time);
    xgyr_mrs->add_elem(gyr_mrs[0]/1000.f, _time);
    ygyr_mrs->add_elem(gyr_mrs[1]/1000.f, _time);
    zgyr_mrs->add_elem(gyr_mrs[2]/1000.f, _time);
    xmag_mT->add_elem(mag_mT[0]/1000.f, _time);
    ymag_mT->add_elem(mag_mT[1]/1000.f, _time);
    zmag_mT->add_elem(mag_mT[2]/1000.f, _time);
}

void MavSystem::track_imu_highres_acc(const float xyz[]) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, x, "IMU-highres/acc/acc x", "m/s/s");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, y, "IMU-highres/acc/acc y", "m/s/s");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, z, "IMU-highres/acc/acc z", "m/s/s");
    x->add_elem(xyz[0], _time);
    y->add_elem(xyz[1], _time);
    z->add_elem(xyz[2], _time);
}

void MavSystem::track_imu_highres_gyr(const float xyz[]) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, x, "IMU-highres/gyro/omg x", "rad/s");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, y, "IMU-highres/gyro/omg y", "rad/s");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, z, "IMU-highres/gyro/omg z", "rad/s");
    x->add_elem(xyz[0], _time);
    y->add_elem(xyz[1], _time);
    z->add_elem(xyz[2], _time);
}

void MavSystem::track_imu_highres_mag(const float xyz[]) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, x, "IMU-highres/mag/field x", "G");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, y, "IMU-highres/mag/field y", "G");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, z, "IMU-highres/mag/field z", "G");
    x->add_elem(xyz[0], _time);
    y->add_elem(xyz[1], _time);
    z->add_elem(xyz[2], _time);
}

void MavSystem::track_imu_highres_temp(float temp_degC) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, d, "IMU-highres/temperature", "deg C");
    d->add_elem(temp_degC, _time);
}

void MavSystem::track_imu_highres_pressabs(const float press_mbar) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, d, "IMU-highres/pressure abs", "mbar");
    d->add_elem(press_mbar, _time);
}

void MavSystem::track_imu_highres_pressalt(float alt_m) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, d, "IMU-highres/pressure altitude", "m");
    d->add_elem(alt_m, _time);
}

void MavSystem::track_imu_highres_pressdiff(float press_mbar) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, d, "IMU-highres/pressure diff", "mbar");
    d->add_elem(press_mbar, _time);
}

void MavSystem::track_imu1(const int16_t acc_mg[], const int16_t gyr_mrs[], const int16_t mag_mT[]) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, xacc_mg, "IMU1/acc/acc x", "g");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, yacc_mg, "IMU1/acc/acc y", "g");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, zacc_mg, "IMU1/acc/acc z", "g");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, xgyr_mrs, "IMU1/gyro/omg x", "rad/s");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, ygyr_mrs, "IMU1/gyro/omg y", "rad/s");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, zgyr_mrs, "IMU1/gyro/omg z", "rad/s");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, xmag_mT, "IMU1/magnetic/mag x", "T");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, ymag_mT, "IMU1/magnetic/mag y", "T");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, zmag_mT, "IMU1/magnetic/mag z", "T");

    xacc_mg->add_elem(acc_mg[0]/1000.f, _time);
    yacc_mg->add_elem(acc_mg[1]/1000.f, _time);
    zacc_mg->add_elem(acc_mg[2]/1000.f, _time);
    xgyr_mrs->add_elem(gyr_mrs[0]/1000.f, _time);
    ygyr_mrs->add_elem(gyr_mrs[1]/1000.f, _time);
    zgyr_mrs->add_elem(gyr_mrs[2]/1000.f, _time);
    xmag_mT->add_elem(mag_mT[0]/1000.f, _time);
    ymag_mT->add_elem(mag_mT[1]/1000.f, _time);
    zmag_mT->add_elem(mag_mT[2]/1000.f, _time);
}

void MavSystem::track_actuators(const uint16_t servo_raw[]) {
    DataTimeseries<unsigned int>*const data_servo[] = {
        MAVSYSTEM_DATA_ITEM_HAVEVAR(DataTimeseries<unsigned int>, "actuators/servo_1", "us"),
        MAVSYSTEM_DATA_ITEM_HAVEVAR(DataTimeseries<unsigned int>, "actuators/servo_2", "us"),
        MAVSYSTEM_DATA_ITEM_HAVEVAR(DataTimeseries<unsigned int>, "actuators/servo_3", "us"),
        MAVSYSTEM_DATA_ITEM_HAVEVAR(DataTimeseries<unsigned int>, "actuators/servo_4", "us"),
        MAVSYSTEM_DATA_ITEM_HAVEVAR(DataTimeseries<unsigned int>, "actuators/servo_5", "us"),
        MAVSYSTEM_DATA_ITEM_HAVEVAR(DataTimeseries<unsigned int>, "actuators/servo_6", "us"),
        MAVSYSTEM_DATA_ITEM_HAVEVAR(DataTimeseries<unsigned int>, "actuators/servo_7", "us"),
        MAVSYSTEM_DATA_ITEM_HAVEVAR(DataTimeseries<unsigned int>, "actuators/servo_8", "us")
    };
    for (unsigned int k=0; k<8; k++) {
        if (data_servo[k]) data_servo[k]->add_elem(servo_raw[k], _time);
    }
}

void MavSystem::track_radio(uint8_t rssi, uint8_t noise, uint16_t rxerr,
                            uint16_t rxerr_corrected, uint8_t txbuf_percent,
                            uint8_t rem_rssi, uint8_t rem_noise) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_rssi, "radio/RSSI", "");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_noise, "radio/noise", "");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_rxerr, "radio/rx errors", "");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_rxerr_corrected, "radio/fixed rx errors", "");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_txbuf, "radio/tx buffer", "%");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_rem_rssi, "radio/remote RSSI", "");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_rem_noise, "radio/remote noise", "");

    data_rssi->add_elem(rssi, _time);
    data_noise->add_elem(noise, _time);
    data_rxerr->add_elem(rxerr, _time);
    data_rxerr_corrected->add_elem(rxerr_corrected, _time);
    data_txbuf->add_elem(txbuf_percent, _time);
    data_rem_rssi->add_elem(rem_rssi, _time);
    data_rem_noise->add_elem(rem_noise, _time);
}

void MavSystem::track_radio(uint8_t rssi) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_rssi, "radio/RSSI", "");
    data_rssi->add_elem(rssi, _time);
}

void MavSystem::track_radio_droprate(float percent) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_drop, "radio/overall drop rate", "");
    data_drop->add_elem(percent, _time);
}

void MavSystem::track_power(float Vcc, float Vservo, uint16_t flags) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_vcc, "power/Vcc", "V");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_vservo, "power/Vservo", "V");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_flags, "power/flags", "MAV_POWER_STATUS");

    data_vcc->add_elem(Vcc, _time);
    data_vservo->add_elem(Vservo, _time);
    data_flags->add_elem(flags, _time);
}

void MavSystem::track_system_errors(uint16_t errors_count [4]) {
    DataTimeseries<unsigned int>*const data_errors[] = {
        MAVSYSTEM_DATA_ITEM_HAVEVAR(DataTimeseries<unsigned int>, "system/error count #1", "AP-specific"),
        MAVSYSTEM_DATA_ITEM_HAVEVAR(DataTimeseries<unsigned int>, "system/error count #2", "AP-specific"),
        MAVSYSTEM_DATA_ITEM_HAVEVAR(DataTimeseries<unsigned int>, "system/error count #3", "AP-specific"),
        MAVSYSTEM_DATA_ITEM_HAVEVAR(DataTimeseries<unsigned int>, "system/error count #4", "AP-specific")
    };
    for (unsigned int k=0; k<4; k++) {
        data_errors[k]->add_elem(errors_count[k], _time);
    }
}

void MavSystem::track_statustext(const char*text, uint8_t severity) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataEvent<string>, data_text, "system/statustext", "string");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_sev, "system/statustext_severity", "int");

    data_text->add_elem(string(text), _time);
    data_sev->add_elem(severity, _time);
    // additional debug
    //_log(MSG_INFO, stringbuilder() << "STATUSTEXT @ t=" << _time << ", severity=" << ((unsigned int)severity) <<": " << text << endl;
}

void MavSystem::track_system_sensors(uint32_t present, uint32_t enabled, uint32_t health) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_spresent, "system/sensors present", "MAV_SYS_STATUS_SENSOR");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_senabled, "system/sensors enabled", "MAV_SYS_STATUS_SENSOR");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<unsigned int>, data_shealth,  "system/sensors health", "MAV_SYS_STATUS_SENSOR");

    data_spresent->add_elem(present, _time);
    data_senabled->add_elem(enabled, _time);
    data_shealth->add_elem(health, _time);
}

void MavSystem::track_nav(float nav_roll_deg, float nav_pitch_deg, float nav_bear_deg,
                          float tar_bear_deg, float wp_dist_m, float err_alt_m,
                          float err_airspeed_ms, float err_xtrack_m) {
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_roll, "navigation/nav roll", "deg");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_pitch, "navigation/nav pitch", "deg");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_bear, "navigation/nav bearing", "deg");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_tbear, "navigation/target bearing", "deg");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_wpdist, "navigation/dist waypoint", "m");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_err_alt, "navigation/error altitude", "m");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_err_speed, "navigation/error airspeed", "m/s");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_err_xtrack, "navigation/error x-track", "m");

    data_roll->add_elem(nav_roll_deg, _time);
    data_pitch->add_elem(nav_pitch_deg, _time);
    data_bear->add_elem(nav_bear_deg, _time);
    data_tbear->add_elem(tar_bear_deg, _time);
    data_wpdist->add_elem(wp_dist_m, _time);
    data_err_alt->add_elem(err_alt_m, _time);
    data_err_speed->add_elem(err_airspeed_ms, _time);
    data_err_xtrack->add_elem(err_xtrack_m, _time);
}

/**
 * @brief POST-PROCESSOR FOR GLIDING PERFORMANCE, position-based
 * computes a/c glide ratio and XXX
 */
void MavSystem::_postprocess_glideperf_pos() {
    /*
     * Need: X, Y, Z position
     */
    const DataTimeseries<float> * data_x = NULL, * data_y = NULL, * data_z = NULL;

    // search for x
    {
        const DataTimeseries<float> * const data_try = get_data <const DataTimeseries<float> >("\\bPN\\b", true);
        if (data_try) {
            data_x = data_try;
        }
    }

    // search for y
    {
        const DataTimeseries<float> * const data_try = get_data <const DataTimeseries<float> >("\\bPE\\b", true);
        if (data_try) {
            data_y = data_try;
        }
    }

    // search for z
    {
        const DataTimeseries<float> * const data_try = get_data <const DataTimeseries<float> >("\\bPD\\b", true);
        if (data_try) {
            data_z = data_try;           
        }
    }

    if (data_x && data_y && data_z) {
        MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_dist, "glideperf/cum. horz. dist.", "m");
        data_dist->set_type(Data::DATA_DERIVED);
        float x_pre, y_pre, hdist_pre;
        for (unsigned k=0; k<data_x->size(); ++k) {
            double t; float x, y, z;
            if (data_x->get_data(k, t, x) && data_y->get_data_at_time(t, y) && data_z->get_data_at_time(t, z)) {
                if (k>0) {
                    const float hdist = sqrt(pow(x-x_pre,2) + pow(y-y_pre,2)) + hdist_pre;
                    data_dist->add_elem(hdist, t);
                    hdist_pre = hdist;
                }
                x_pre = x; y_pre = y;
            } else {
                _log(MSG_WARN, stringbuilder() << " #" << id << ": postproc/glideperf: failed getting position data");
            }
        }
    }
}


/**
 * @brief POST-PROCESSOR FOR GLIDING PERFORMANCE, velocity-based
 * computes a/c glide ratio and XXX
 */
void MavSystem::_postprocess_glideperf_vel() {
    /*
     *  we need the following four timeseries:
     *  - vd: sink speed
     *  - airspeed: ...
     *  - pitch: only if pitch is approx. constant for some time we compute glide ratio,
     *           otherwise it is likely that lift/sink comes from consuming kinetic or
     *           potential energy
     *  - roll: we use the roll angle to compensate/correct the glide ratio, because when
     *          turning we loose sin(roll) of lift
     */

    const float SPEED_MIN = 5.f;
    const float ACCX_MAX = 2.0; ///< max. 2m/s/s in airspeed change
    const float PITCH_MAX = 20.f;
    const float ROLL_MAX = 45.f;

    // we need to find data for these:
    const DataTimeseries<float> * data_roll = NULL, * data_pitch = NULL, * data_sink = NULL,
                                * data_airspeed = NULL, *data_accx = NULL, *data_gspeed = NULL,
                                * data_windN = NULL, *data_windE = NULL, *data_yaw = NULL;
    bool have_wind = false;

    // search for roll angle
    {
        const DataTimeseries<float> * const data_try = get_data <const DataTimeseries<float> >("\\b[rR]oll\\b", true);
        if (data_try) {
            data_roll = data_try;
        }
    }

    // search for accx
    {
        const DataTimeseries<float> * const data_try = get_data <const DataTimeseries<float> >("\\bAccX\\b", true);
        if (data_try) {
            data_accx = data_try;
        }
    }

    // search for pitch angle
    {
        const DataTimeseries<float> * const data_try = get_data <const DataTimeseries<float> >("\\b[pP]itch\\b", true);
        if (data_try) {
            data_pitch = data_try;
        }
    }

    // search for wind estimate
    {
        const DataTimeseries<float> * const data_try1 = get_data <const DataTimeseries<float> >("\\bVWE\\b", true);
        const DataTimeseries<float> * const data_try2 = get_data <const DataTimeseries<float> >("\\bVWN\\b", true);
        const DataTimeseries<float> * const data_try3 = get_data <const DataTimeseries<float> >("\\bYaw\\b", true);
        if (data_try1 && data_try2 && data_try3) {
            // no check. if there is a dataseries, we believe in it.
            data_windE = data_try1;
            data_windN = data_try2;
            data_yaw = data_try3;
            have_wind = true;
        }
    }

    // search for airspeed ..
    {
        const DataTimeseries<float> * const data_try = get_data <const DataTimeseries<float> >("\\bTrueSpeed\\b", true);
        if (data_try) {
            // check if this is actually has sensor data
            if (data_try->get_max() - data_try->get_min() > SPEED_MIN) {
                data_airspeed = data_try;
            } else {
                _log(MSG_WARN, stringbuilder() << " #" << id << ": postproc/glideperf: ignoring airspeed '" << data_try->get_fullname(data_try) << "' because of low variance");
            }
        }
    }

    // search for ground speed
    {
        // FIXME: otherwise, try to get speed in N,E,D/X,Y,Z directions and compute vector length
        const DataTimeseries<float> * const data_try1 = get_data <const DataTimeseries<float> >("NKF1/VE", true);
        const DataTimeseries<float> * const data_try2 = get_data <const DataTimeseries<float> >("NKF1/VN", true);
        if (data_try1 && data_try2) {
            // if present, we believe the data
            // fuse to scalar
            MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_newspeed, "glideperf/groundspeed", "VE and VN");
            for (unsigned int k=0; k < data_try1->size(); ++k) {
                double t; float vn, ve;
                if (data_try1->get_data(k, t, ve) && data_try2->get_data_at_time(t, vn)) {
                    float total = sqrt(pow(ve,2) + pow(vn,2));
                    data_newspeed->add_elem(total, t);
                } else {
                    _log(MSG_WARN, stringbuilder() << " #" << id << ": postproc/glideperf: failed getting ground speed");
                }
            }
            data_newspeed->set_type(Data::DATA_DERIVED);
            data_gspeed = data_newspeed;
        }
    }

    if (!data_gspeed) {
        const DataTimeseries<float> * const data_try = get_data <const DataTimeseries<float> >("GPS/Spd", true);
        if (data_try) {
            // check if this is actually has sensor data
            if (data_try->get_max() - data_try->get_min() > SPEED_MIN) {
                data_gspeed = data_try;
            }
        }
    }


    // search for sink speed.. try best data source first, then go to worse sources
    {
        const DataTimeseries<float> * const data_try = get_data <const DataTimeseries<float> >("\\bVD\\b", true);
        if (data_try) {
            data_sink= data_try;
        }
    }
    if (!data_sink) {
        const DataTimeseries<float> * const data_try = get_data <const DataTimeseries<float> >("GPS/VZ", true);
        if (data_try) {
            data_sink= data_try;
        }
    }

    // messages for user
    if (!data_roll) {
        _log(MSG_ERR, stringbuilder() << " #" << id << ": postproc/glideperf: roll angle not found in data");
    } else {
        _log(MSG_INFO, stringbuilder() << " #" << id << ": postproc/glideperf: using roll angle '" << data_roll->get_fullname(data_roll) << "'");
    }
    if (!data_accx) {
        _log(MSG_ERR, stringbuilder() << " #" << id << ": postproc/glideperf: acc x not found in data");
    } else {
        _log(MSG_INFO, stringbuilder() << " #" << id << ": postproc/glideperf: using acc x '" << data_accx->get_fullname(data_accx) << "'");
    }
    if (!data_pitch) {
        _log(MSG_ERR, stringbuilder() << " #" << id << ": postproc/glideperf: pitch angle not found in data");
    } else {
        _log(MSG_INFO, stringbuilder() << " #" << id << ": postproc/glideperf: using pitch angle '" << data_pitch->get_fullname(data_pitch) << "'");
    }
    if (!data_sink) {
        _log(MSG_ERR, stringbuilder() << " #" << id << ": postproc/glideperf: sink speed not found in data");
    } else {
        _log(MSG_INFO, stringbuilder() << " #" << id << ": postproc/glideperf: using sink speed '" << data_sink->get_fullname(data_sink) << "'");
    }
    if (!data_gspeed) {
        if (!data_airspeed) _log(MSG_ERR, stringbuilder() << " #" << id << ": postproc/glideperf: groundspeed not found in data");
    } else {
        _log(MSG_INFO, stringbuilder() << " #" << id << ": postproc/glideperf: using groundspeed '" << data_gspeed->get_fullname(data_gspeed) << "'");
    }
    if (!data_airspeed) {
        if (!data_gspeed) {
            _log(MSG_ERR, stringbuilder() << " #" << id << ": postproc/glideperf: airspeed not found in data");
        } else {
            if (!have_wind) {
                _log(MSG_WARN, stringbuilder() << " #" << id << ": postproc/glideperf: airspeed not found, but groundspeed without wind estimates. Results may be bogus.");
            } else {
                _log(MSG_INFO, stringbuilder() << " #" << id << ": postproc/glideperf: airspeed is reconstructed from groundspeed and wind estimates.");
            }
        }
    } else {
        _log(MSG_INFO, stringbuilder() << " #" << id << ": postproc/glideperf: using airspeed '" << data_airspeed->get_fullname(data_airspeed) << "'");
    }
    if (have_wind) {
        _log(MSG_INFO, stringbuilder() << " #" << id << ": postproc/glideperf: using wind '" << data_windE->get_fullname(data_windE) << "' and related");
    }
    if (!((data_airspeed || data_gspeed) && data_pitch && data_roll && data_sink && data_accx)) return;


    // finally...compute glide ratio
    {
        MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_glideratio, "glideperf/glide ratio", "ratio");
        data_glideratio->set_type(Data::DATA_DERIVED);
        data_glideratio->clear();

        // do wind first, if any
        if (have_wind) {
            MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_winddir, "glideperf/wind direction", "degree, coming from (aeronautic convention)");
            MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_windspd, "glideperf/wind speed", "same units as VWE and VWN");
            data_winddir->set_type(Data::DATA_DERIVED);
            data_windspd->set_type(Data::DATA_DERIVED);
            for (unsigned int k=0; k < data_windE->size(); ++k) {
                double t; float wE, wN;
                data_windE->get_data(k, t, wE); // wind blowing towards east direction
                data_windN->get_data_at_time(t, wN); // wind blowing towards north direction

#if 0
                // DEBUG: wind blowing from south to north (180°)
                wE = 0.;
                wN = 4.;
#endif
                float winddir = 180./M_PI*atan2(-wE, -wN); // [0,0]=>0, [1,0]=>270°, [0,1]=>180°, [-1, 0]=>90°
                winddir = angle360(winddir);
                data_winddir->add_elem(winddir, t);
                float windspd = sqrt(pow(wE,2.) + pow(wN,2.));
                data_windspd->add_elem(windspd, t);

                // compute moew cool things
                float yaw; data_yaw->get_data_at_time(t, yaw);
                yaw = DEG2RAD(angle360(yaw));

                // aeronautic: if wind direction is opposite of yaw, then it's tail wind. So flip it.
                float winddir_inv = DEG2RAD(angle360(winddir-180.));
                float windrel = acos(cos(winddir_inv) * cos(yaw) + sin(winddir_inv) * sin(yaw));
                float windhd = -cos(windrel)*windspd;
                MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_windrel, "glideperf/relative wind angle", "degree between yaw angle and wind direction");
                MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_windhd, "glideperf/head wind", "same units as VWE and VWN");
                data_windrel->set_type(Data::DATA_DERIVED);
                data_windhd->set_type(Data::DATA_DERIVED);
                data_windrel->add_elem(RAD2DEG(windrel), t);
                data_windhd->add_elem(windhd, t);

                if (data_gspeed) {
                    // we estimate airspeed...even when there is a sensor. That is a good exercise.
                    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_airspeedest, "glideperf/airspeed estimate", "same units as VWE and VWN");
                    data_airspeedest->set_type(Data::DATA_DERIVED);
                    float airspeed = 0.; data_gspeed->get_data_at_time(t, airspeed);
                    airspeed += windhd; // compensate with headwind
                    data_airspeedest->add_elem(airspeed, t);
                }
            }
        }


        // where sink is > 0 ...
        float maxratio = 0.;
        float optspeed = 0.;
        for (unsigned int k=0; k < data_sink->size(); ++k) {
            double t;
            float sink;            
            if (data_sink->get_data(k, t, sink)) {
                if (sink > 0.) {
                    float airspeed = 0.f, pitch=0.f, roll=0.f, accx=0.f;
                    data_accx->get_data_at_time(t, accx);
                    if (data_airspeed) {
                        data_airspeed->get_data_at_time(t, airspeed);
                    } else {
                        if (have_wind) {
                            MAVSYSTEM_REQUIRE_DATA(DataTimeseries<float>, data_airspeedest, "glideperf/airspeed estimate");
                            data_airspeedest->get_data_at_time(t, airspeed);
                        } else {
                            data_gspeed->get_data_at_time(t, airspeed);
                        }
                    }
                    data_pitch->get_data_at_time(t, pitch);
                    data_roll->get_data_at_time(t, roll); // FIXME: check units. Some Autopilots may have scaling

                    // detect stationary flight: no kinectic energy being tranformed -> derivative of speed (AccX) close to zero
                    // and around normal attitude and moving ...
                    if (airspeed > SPEED_MIN && fabs(pitch) < PITCH_MAX && fabs(roll) < ROLL_MAX && fabs(accx) < ACCX_MAX) {
                        float ratio = airspeed / sink;
                        ratio = ratio / cos(M_PI*fabs(roll)/180.);
                        data_glideratio->add_elem(ratio, t);
                        if (ratio > maxratio) {
                            maxratio = ratio;
                            optspeed = airspeed;
                        }
                    }
                }
            }
        }

        MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_glideratio5, "glideperf/glide ratio 5sec avg", "ratio");
        data_glideratio->moving_average(*data_glideratio5, 5.0);

        // TODO: phenomenologic glide ratio from distance traveled vs altitude loss

        if (maxratio > 0.) {
            _log(MSG_INFO, stringbuilder() << " #" << id << ": postproc/glideperf: Estimated max. A/C glide ratio of " << maxratio << " at speed " << optspeed);
        }
    }
}

/**
 * @brief POST-PROCESSOR FOR POWER STATISTICS
 * computes charge, power and cumulated versions of them
 */
void MavSystem::_postprocess_powerstats() {
    MAVSYSTEM_REQUIRE_DATA(DataTimeseries<float>, data_battery_volt, "power/battery_voltage");
    MAVSYSTEM_REQUIRE_DATA(DataTimeseries<float>, data_battery_amps, "power/battery_current");

    // if we are here, then this postprocessing module may run, because it has all the data it needs.
    if (data_battery_volt->get_epoch_datastart() != data_battery_amps->get_epoch_datastart()) {
        _log(MSG_WARN, stringbuilder() << " #" << id << ": postproc/powerstats: cannot work un unsync'd data.");
        return;
    }
    unsigned long epoch_datastart_usec = data_battery_amps->get_epoch_datastart();

    // generate power consumption
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_power, "power/power", "W");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_consumption, "power/inst. consumption", "Ws");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_charge, "power/inst. charge", "As");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_cconsumption, "power/cum. consumption", "Wh");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataTimeseries<float>, data_ccharge, "power/cum. charge", "Ah");
    data_power->set_type(Data::DATA_DERIVED);
    data_consumption->set_type(Data::DATA_DERIVED);
    data_cconsumption->set_type(Data::DATA_DERIVED);
    data_charge->set_type(Data::DATA_DERIVED);
    data_ccharge->set_type(Data::DATA_DERIVED);

    // at this point, make sure the data we are writing is empty, since postprocess could be called multiple times
    data_power->clear();
    data_consumption->clear();
    data_cconsumption->clear();
    data_charge->clear();
    data_ccharge->clear();
    data_power->set_epoch_datastart(epoch_datastart_usec);
    data_consumption->set_epoch_datastart(epoch_datastart_usec);
    data_cconsumption->set_epoch_datastart(epoch_datastart_usec);
    data_charge->set_epoch_datastart(epoch_datastart_usec);
    data_ccharge->set_epoch_datastart(epoch_datastart_usec);

    /*************************************
     *  POWER: TODO: union of samples
     *************************************/
    {
        for (unsigned int k=0; k < data_battery_volt->size(); ++k) {
            float volt;
            double t;
            if (data_battery_volt->get_data(k, t, volt)) {
                float amps;
                if (data_battery_amps->get_data_at_time(t, amps)) {
                    float power_est_watts = volt*amps;
                    data_power->add_elem(power_est_watts, t);
                }
            }
        }
    }

    /***********************************
     *  CHARGE
     ***********************************/
    {
        float ccharge_As=0.f;
        double fa, fb, ta, tb;
        for (unsigned int k=0; k < data_battery_amps->size(); ++k) {
            double t;
            float current;
            if (data_battery_amps->get_data(k,t,current)) {
                if (k==0) {
                    ta = t;
                    fa = current;
                    data_charge->add_elem(0.f, t);
                    data_ccharge->add_elem(0.f, t);
                } else {
                    tb = t;
                    fb = current;
                    double charge = (tb-ta)*(fa+fb)/2.; //As
                    ccharge_As += charge;
                    // add data to rows
                    data_charge->add_elem((float)charge, t); // As = C
                    data_ccharge->add_elem((float)ccharge_As/3600., t); // As -> Ah
                    // for next step
                    ta = tb;
                    fa = fb;
                }
            }
        }
    }

    /***********************************
     *  POWER CONSUMPTION: now go through the data and integrate (trapezoidal rule) the power to get power consumption
     ***********************************/
    {
        float cconsumption_Ws=0.f;
        double fa, fb, ta, tb;
        for (unsigned int k=0; k < data_power->size(); ++k) {
            double t;
            float power;
            if (data_power->get_data(k,t,power)) {
                if (k==0) {
                    ta = t;
                    fa = power;
                    data_consumption->add_elem(0.f, t);
                    data_cconsumption->add_elem(0.f, t);
                } else {
                    tb = t;
                    fb = power;
                    double consumption = (tb-ta)*(fa+fb)/2.; // Ws
                    cconsumption_Ws += consumption;
                    // add data to rows
                    data_consumption->add_elem((float)consumption, t); // Ws
                    data_cconsumption->add_elem((float)cconsumption_Ws/3600., t); // Ws -> Wh
                    // for next step
                    ta = tb;
                    fa = fb;
                }
            }
        }
    }

    _log(MSG_INFO, stringbuilder() << " #" << id << ": postproc/powerstats: DONE." );
}

/**
 * @brief POST-PROCESSOR FOR TIMING:
 * Some timeseries have inaccurate/bad timestamps. Here we assume that those messages
 * are periodic, and we distribute them equi-distantly over the entire time span.
 */
void MavSystem::_postprocess_bad_timing() {

    // first take a copy of the datamap, and then iterate that copy. This is because
    // we are appending to the list here, which would otherwise loop infinitely
    data_accessmap oldmap = _data_from_path;

    for (data_accessmap::const_iterator it = oldmap.begin(); it != oldmap.end(); ++it) {
        Data*const d = it->second;
        if (d) {
            DataTimed*ds = dynamic_cast<DataTimed*>(d);
            if (ds && ds->has_bad_timestamps()) {
                // create a backup
                Data*data_orig = ds->Clone();
                if (data_orig) {
                    const std::string bak_name = ds->get_name() + "_orig";
                    data_orig->set_name(bak_name);
                    const std::string fullname = Data::get_fullname(data_orig);
                    _data_register_hierarchy(fullname, data_orig);
                }

                // re-align timing
                ds->make_periodic();
                const std::string msg = "fixed timing of " + ds->get_name() + " (made periodic)";
                Logger::Instance().write(MSG_INFO, msg, _logchannel);
            }
        }
    }
}

/**
 * @brief POST-PROCESSOR FLIGHTBOOK: number of flights, first take-off, last landing, flight time
 * This is an example, how "computed data" can be produced based on other (raw or computed) data.
 */
void MavSystem::_postprocess_flightbook() {
    /*
     * the REQUIRE macro gets pointer to the data, if available. If not, it returns and this function does nothing.
     */
    MAVSYSTEM_REQUIRE_DATA(DataTimeseries<float>, data_alt, "airstate/alt GND");
    MAVSYSTEM_REQUIRE_DATA(DataTimeseries<float>, data_throttle_percent, "airstate/throttle");

    // if we are here, then this postprocessing module may run, because it has all the data it needs.    
    if (data_alt->get_epoch_datastart() != data_throttle_percent->get_epoch_datastart()) {
        _log(MSG_WARN, stringbuilder() << " #" << id << ": postproc/flightbook: cannot work un unsync'd data.");
        return;
    }
    unsigned long epoch_datastart_usec = data_alt->get_epoch_datastart();

    // generate new event data series
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataEvent<string>, evt_takeofflanding, "flightbook/takeoff_landing", "");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataParam<unsigned int>, data_nflights, "flightbook/number flights", "");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataParam<double>, data_flighttime, "flightbook/total flight time", "s");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataParam<double>, data_first_takeoff, "flightbook/first takeoff", "[time epoch]");
    MAVSYSTEM_DATA_ITEM_OR_RETURN(DataParam<double>, data_last_landing, "flightbook/last landing", "[time epoch]");
    evt_takeofflanding->set_type(Data::DATA_DERIVED);
    data_nflights->set_type(Data::DATA_DERIVED);
    data_flighttime->set_type(Data::DATA_DERIVED);
    data_first_takeoff->set_type(Data::DATA_DERIVED);
    data_last_landing->set_type(Data::DATA_DERIVED);

    // at this point, make sure the data we are writing is empty, since postprocess could be called multiple times
    evt_takeofflanding->clear();
    data_nflights->clear();
    data_flighttime->clear();
    data_first_takeoff->clear();
    data_last_landing->clear();
    evt_takeofflanding->set_epoch_datastart(epoch_datastart_usec);
    data_nflights->set_epoch_datastart(epoch_datastart_usec);
    data_flighttime->set_epoch_datastart(epoch_datastart_usec);
    data_first_takeoff->set_epoch_datastart(epoch_datastart_usec);
    data_last_landing->set_epoch_datastart(epoch_datastart_usec);

    // now go through the raw data and try to identify takeoff and landing
    bool flying = false;

    // use altitude as primary data and go through it. interpolate throttle
    double t, t_takeoff=0;
    double t_first_takeoff=0.;
    double t_last_landing=0.;
    unsigned int nflights=0;
    double flighttime = 0.;    
    for (unsigned int k=0; k<data_alt->size(); ++k) {
        float alt;
        if (data_alt->get_data(k, t, alt)) {
            float throttle;
            if (data_throttle_percent->get_data_at_time(t, throttle)) {
                // flying == alt > 0 && throttle > 20
                bool seems_flying = alt > 1. && throttle > 20.f;
                // FIXME: debounce
                if (seems_flying && !flying) {
                    flying = true;
                    evt_takeofflanding->add_elem("takeoff", t);
                    nflights++;
                    if (nflights==1) { t_first_takeoff = t; }
                    t_takeoff = t;
                } else if (!seems_flying && flying) {
                    flying = false;
                    evt_takeofflanding->add_elem("landing", t);
                    t_last_landing = t;
                    flighttime+=(t-t_takeoff);
                }
            }
        }
    }
    data_nflights->add_elem(nflights);
    data_flighttime->add_elem(flighttime);
    data_first_takeoff->add_elem(t_first_takeoff);
    data_last_landing->add_elem(t_last_landing);
    _log(MSG_INFO, stringbuilder() << " #" << id  << ": postproc/flightbook: DONE.");
}

void MavSystem::postprocess() {
    _postprocess_bad_timing();
    _postprocess_flightbook();
    _postprocess_powerstats();    
    _postprocess_glideperf_pos();
    _postprocess_glideperf_vel();
    // hook more postprocessing functions in here, if you write new ones.
}

int MavSystem::update_rel_time(uint64_t nowtime_relative_usec, bool allowjumps){
    double cand_time = nowtime_relative_usec/1E6;

    // sanity check: refuse time stamps that are temporally too far apart from each other
    double diff = 0.;
    if (_time_valid) {
        diff = cand_time - _time; // FIXME: doesn't work.
    } else {
        _time_valid = true; // we trust the first sample...
    }

    int ret = 0;    
    if (diff < -_time_maxbackjump_sec && !allowjumps) {
        _log(MSG_WARN, stringbuilder() << " # " << id << " !!! ignoring timestamp that is too old: -" << diff << " s");
        ret = -1; // backward jump
    } else if ((diff > _time_maxfwdjump_sec) && !allowjumps) {
        /* some MavLink tlogs actually *have* huge jumps that are correct. Two reasons:
         *  1. presumably such files just don't start soon after boot, but some time later,
         *     but carry a close-zero time stamp in the first message due to no GPX fix
         *  2. when GCS logs multiple flights of the same vehicle w/o disconnecting in between
         */
        _log(MSG_WARN, stringbuilder() << " # " << id << " !!! ignoring timestamp that fast-forwarded by " << diff << " s");
        ret = 1; // forward jmp
    } else {
        if (cand_time < _time_min) _time_min = cand_time;
        if (cand_time > _time_max) _time_max = cand_time;
        _time = cand_time;
        _have_time_update = true;
        //_log(MSG_DBG, stringbuilder() << "#" << id<< ": t=" << nowtime_relative_usec);
    }
    return ret;
}

double MavSystem::get_time_active_end() const {
    unsigned long tmax;
    if (deferredLoad) {
        // data not available, make a guess
        tmax = _time_max*1E6 + _time_offset_usec;
    } else {
        tmax = get_time_active_end_usec();
    }
    const double epoch_sec = tmax / 1E6;
    return epoch_sec;
}

unsigned long MavSystem::get_time_active_end_usec() const {
    unsigned long tmax=0;

    /******************************************
     *  FIND MAX TIME OF ALL DATA
     ******************************************/
    for (data_accessmap::const_iterator it = _data_from_path.begin(); it != _data_from_path.end(); ++it) {
        const Data*const d = it->second;
        if (d) {
            unsigned long tmax_epoch_usec = d->get_epoch_dataend();
            if (tmax_epoch_usec > tmax) { tmax = tmax_epoch_usec; }
        }
    }
    return tmax;
}

double MavSystem::get_time_active_begin(void) const {    
    unsigned long tmin;
    if (deferredLoad) {
        // data not available, make a guess
        tmin = _time_min*1E6 + _time_offset_usec;
    } else {
        tmin = get_time_active_begin_usec();
    }
    double epoch_sec = tmin / 1E6;
    return epoch_sec;
}

unsigned long MavSystem::get_time_active_begin_usec(void) const {
    unsigned long tmin = ULONG_MAX;
    /******************************************
     *  FIND MIN TIME OF ALL DATA
     ******************************************/
    for (data_accessmap::const_iterator it = _data_from_path.begin(); it != _data_from_path.end(); ++it) {
        const Data*const d = it->second;
        if (d) {
            unsigned long tmin_epoch_usec = d->get_epoch_datastart();
            if (tmin_epoch_usec < tmin) { tmin = tmin_epoch_usec; }
        }
    }
    return tmin;
}

void MavSystem::get_mavlink_stats(MavSystem::mavlink_summary_t & ret) const {
    ret = _mavlink_summary; // deep copy
}

// DONE
bool MavSystem::merge_in(const MavSystem * const other) {
    // copy data inside, the datagroup is not copied but created with our own functions again
    bool added=false;
    for (data_accessmap::const_iterator ito = other->_data_from_path.begin(); ito != other->_data_from_path.end(); ++ito) {
        const Data*const data = ito->second;
        if (!_add_data(data)) {
            _log(MSG_WARN, stringbuilder() << "WARNING: skipped data " << data->get_name() << " because it could not be merged");
            // return false;
        } else {
            added = true;
        }
    }
    if (added) {
        postprocess();
        determine_absolute_time();
    }
    return true;
}

void MavSystem::update_time_offset_guess(uint64_t nowtime_relative_usec, uint64_t epoch_usec) {
    assert(nowtime_relative_usec <= epoch_usec); // FIXME: assert is böse
    if (epoch_usec > 0) {  _time_offset_guess_usec = epoch_usec - nowtime_relative_usec; }
}

bool MavSystem::is_absolute_time(uint64_t timestamp_usec) const {
    // simple but effective: convert to yyyy-mm-dd; if it is earlier y2k, then assume relative time
    struct tm when = epoch_to_tm(timestamp_usec/1E6);
    when.tm_year += 1900; // because it is since 1900
    return (when.tm_year > 2000);
}

void MavSystem::update_time_offset(uint64_t nowtime_relative_usec, uint64_t epoch_usec, bool allowjumps) {
    update_rel_time(nowtime_relative_usec, allowjumps);

    if (epoch_usec > 0) { // sanity check
        _time_offset_raw.push_back(timeoffset_pair(nowtime_relative_usec, epoch_usec));
    }
}


void MavSystem::shift_time(double delay) {
    // add both to _time_offset_raw to _time_offset_guess_usec
    int64_t udelay = delay*1E6;
    for (vector<timeoffset_pair >::iterator it = _time_offset_raw.begin(); it != _time_offset_raw.end(); ++it) {
        it->first -= udelay;
    }
    _time_offset_guess_usec += udelay;
}

void MavSystem::determine_absolute_time() {
    // FIXME: this could overwrite data's epoch_start, e.g., when merged.
    if (!_time_offset_raw.empty()) {
        double sum_diff = 0;
        double diff;
        for (vector<timeoffset_pair >::const_iterator it = _time_offset_raw.begin(); it != _time_offset_raw.end(); ++it) {
            // epoch - relative time
            diff = it->second - it->first;
            sum_diff += diff;
        }
        sum_diff /= _time_offset_raw.size();
        _time_offset_usec = (uint64_t)(round(sum_diff));
    } else {
        // make a guess        
        _time_offset_usec = _time_offset_guess_usec;
        _log(MSG_WARN, stringbuilder() << "(#" << id<< "): no time reference in the file; making a guess: " << epoch_to_datetime(_time_offset_usec/1E6));
    }    

    // apply to all data
    for (data_accessmap::iterator it = _data_from_path.begin(); it != _data_from_path.end(); ++it) {
        it->second->set_epoch_datastart(_time_offset_usec);
    }
}
