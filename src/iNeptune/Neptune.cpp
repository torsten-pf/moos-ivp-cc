/************************************************************/
/*    NAME: Conlan Cesar                                    */
/*    ORGN: MIT                                             */
/*    FILE: Neptune.cpp                             */
/*    DATE: Spring 2020                                     */
/************************************************************/

#include <iterator>
#include "MBUtils.h"
#include "ACTable.h"
#include "Neptune.h"

using namespace std;

//---------------------------------------------------------
// Constructor

Neptune::Neptune()
{
}

//---------------------------------------------------------
// Destructor

Neptune::~Neptune()
{
}

string Neptune::genNMEAChecksum(string nmeaString) {
  unsigned char xCheckSum = 0;
  string::iterator p;

  // Get the text between the $ and *
  MOOSChomp(nmeaString,"$");
  string sToCheck = MOOSChomp(nmeaString,"*");

  // XOR every byte as a checksum
  for (p = sToCheck.begin(); p != sToCheck.end(); p++) {
    xCheckSum ^= *p;
  }

  ostringstream os;
  os.flags(ios::hex);
  os << (int) xCheckSum;
  string sExpected = os.str();

  if (sExpected.length() < 2)
    sExpected = "0" + sExpected;

  return sExpected;
}

string Neptune::genMONVGString() {
  const int precision = 8;
  // Very similar to CPNVG from https://oceanai.mit.edu/herons/docs/ClearpathWireProtocolV0.2.pdf
  // $MONVG,timestampOfLastMessage,lat,lon,quality(1good 0bad),altitude,depth,heading,roll,pitch,speed*
  string nmea = "$MONVG,";
  std::stringstream ss;
  ss << std::put_time(std::localtime(&m_last_updated_time), "%H%M%S,");
  nmea += ss.str();
  nmea += doubleToString(m_latest_lat, precision) + "," + doubleToString(m_latest_long, precision) + ",1," + doubleToString(m_latest_alt, precision) +
    "," + doubleToString(m_latest_depth, precision) + "," + doubleToString(m_latest_heading, precision) +  ",,," + doubleToString(m_latest_speed, precision) + "*";
  nmea += genNMEAChecksum(nmea);
  return nmea;
}

string Neptune::genMOVALString(std::string key, std::string value, time_t time) {
  string nmea = "$MOVAL,";
  std::stringstream ss;
  ss << std::put_time(std::localtime(&time), "%H%M%S,");
  nmea += ss.str();
  nmea += key + "," + value + "*";
  nmea += genNMEAChecksum(nmea);
  return nmea;
}

double Neptune::timeDifferenceFromNow(const string& t2) {
  const time_t currtime = m_curr_time;

  struct std::tm* tm = localtime(&currtime); // Assume we have the same timezone
  std::istringstream ss(t2);
  ss >> std::get_time(tm, "%H%M%S"); // Override hour, minute, second
  //strptime(sent_time.c_str(), "%H%M%S", &tm);
  time_t tx_unix_time = mktime(tm);

  return abs(currtime - tx_unix_time);
}

bool Neptune::failsTimeCheck(const string& t2, double& diff) {
  if (maximum_time_delta < 0) {
    return false; // If time delta is below zero, consider it disabled
  }
  double df = timeDifferenceFromNow(t2);
  diff = df;
  return (df > maximum_time_delta);
}

void Neptune::handleIncomingNMEA(const string _rx) {
  string rx = _rx;
  MOOSTrimWhiteSpace(rx);

  // Verify Checksum
  string nmeaNoChecksum = rx;
  string checksum = rbiteString(nmeaNoChecksum, '*');
  string expected = genNMEAChecksum(nmeaNoChecksum);
  if (!MOOSStrCmp(expected, checksum) && validate_checksum) {
    reportRunWarning("Expected checksum " + expected + " but got " + checksum + ", ignoring message");
    reportEvent("Dropped Message: " + rx);
    return;
  }
  // All messages follow $KEYHI,TimestampSent,the rest*XX

  // Process Message
  string key = biteStringX(nmeaNoChecksum, ',');
  string sent_time = biteStringX(nmeaNoChecksum, ',');

  // Check Time
  double diff = -1;
  if (failsTimeCheck(sent_time, diff)) {
    reportRunWarning("Time difference " + doubleToString(diff) + ">" + doubleToString(maximum_time_delta) + ", ignoring message " + key);
    return;
  }

  // Process Message
  if (MOOSStrCmp(key, "$MOPOK")) {
    // $MONVG,timestampOfLastMessage,KEY=val*
    // Poke a MoosDB message
    string mail = biteStringX(nmeaNoChecksum, '=');
    string val = nmeaNoChecksum; // the only remaining text should be the value to set
    if (isNumber(val)) {
      Notify(mail, stod(val));
    } else {
      Notify(mail, val);
    }
  } else if (MOOSStrCmp(key, "$MOREG")) {
    // Register a moos variable
    // $MOREG,timestamp,Key To Register,dfInterval*
    string regKey = biteStringX(nmeaNoChecksum, ',');
    double dfInterval = stod(biteStringX(nmeaNoChecksum, ','));

    forward_mail.push_back(regKey);
    Register(regKey, dfInterval);
  } else {
    reportRunWarning("Unhandled Command: " + key);
  }
}

//---------------------------------------------------------
// Procedure: OnNewMail

bool Neptune::OnNewMail(MOOSMSG_LIST &NewMail)
{
  AppCastingMOOSApp::OnNewMail(NewMail);

  MOOSMSG_LIST::iterator p;
  for(p=NewMail.begin(); p!=NewMail.end(); p++) {
    CMOOSMsg &msg = *p;
    string key    = msg.GetKey();

#if 0 // Keep these around just for template
    string comm  = msg.GetCommunity();
    double dval  = msg.GetDouble();
    string sval  = msg.GetString(); 
    string msrc  = msg.GetSource();
    double mtime = msg.GetTime();
    bool   mdbl  = msg.IsDouble();
    bool   mstr  = msg.IsString();
#endif

    if (vectorContains(forward_mail, key, false)) {
      send_queue.push_back(genMOVALString(key, msg.GetAsString(), m_curr_time));
    }

    if (key == "APPCAST_REQ") return true;

     if(key == "NAV_HEADING") {
       m_latest_heading = msg.GetDouble();
     } else if (key == "NAV_SPEED") {
       m_latest_speed = msg.GetDouble();
     } else if (key == "NAV_DEPTH") {
       m_latest_depth = msg.GetDouble();
     }  else if (key == "NAV_LAT") {
       m_latest_lat = msg.GetDouble();
     } else if (key == "NAV_LONG") {
       m_latest_long = msg.GetDouble();
     } else if (key == "NAV_ALTITUDE") {
       m_latest_alt = msg.GetDouble();
     } else {
       reportRunWarning("Unhandled unrequested mail: " + key);
       return true;
     }
     m_last_updated_time = m_curr_time;

   }
	
   return(true);
}

//---------------------------------------------------------
// Procedure: OnConnectToServer

bool Neptune::OnConnectToServer()
{
  registerVariables();
  return(true);
}

//---------------------------------------------------------
// Procedure: ConnectToNMEAServer

bool Neptune::ConnectToNMEAServer()
{
  m_last_nmea_connect_time = m_curr_time;
  m_server.close();

  std::string niceAddr = m_connect_addr + ":" + intToString(m_connect_port);
  reportEvent("Attempting to connect to server @ " + niceAddr);

  if (!m_server.create()) {
    reportRunWarning("Failed to create socket");
    m_server.close();
    return false;
  }

  std::string host = m_connect_addr;

  // Resolve hostname
  struct addrinfo* result;
  int error = getaddrinfo(host.c_str(), nullptr, nullptr, &result);
  if (error) {
    std::string errstr = gai_strerror(error);
    reportRunWarning("Failed to resolve hostname: " + errstr);
    return false;
  } else {
    char ipchar[INET_ADDRSTRLEN];
    if (getnameinfo(result->ai_addr, result->ai_addrlen, ipchar, sizeof(ipchar), nullptr, 0, NI_NUMERICHOST) == 0) {
      host = std::string(ipchar);
    } else {
      reportRunWarning("Unable to parse given host, using " + host);
    }
  }

  int retval = m_server.connect(host, m_connect_port);
  if (retval) {
    reportRunWarning("Failed to connect to server");
    std::string err = strerror(retval);
    reportEvent("Failure Reason: " + err);
    m_server.close();
    return false;
  }
  m_server.set_non_blocking(true);

  return true;
}

//---------------------------------------------------------
// Procedure: Iterate()
//            happens AppTick times per second

bool Neptune::Iterate()
{
  AppCastingMOOSApp::Iterate();

  if (m_server.is_valid()) {
    send_queue.push_back(genMONVGString());
    // Tx NMEA String to server
    while (!send_queue.empty()) {
      std::string val = send_queue.back();
      send_queue.pop_back();
      Notify("SENT_NMEA_MESSAGE", val);
      int retval = m_server.send(val + "\n");
      if (retval) {
        std::string err = strerror(retval);
        reportRunWarning("Lost connection to server: " + err);
        m_server.close();
      }
    }

    // Rx
    std::string rx;
    int len = m_server.recv(rx); // TODO explicit error checking
    if (len > 0) {
      // Check if we have multiple incoming strings
      for (string& rxi : parseString(rx, "\n")) {
        // Check if we have a valid NMEA string
        if (rxi.rfind('$', 0) == 0) {
          // Process NMEA string
          Notify("INCOMING_NMEA", rxi);
          handleIncomingNMEA(rxi);
        } else {
          MOOSTrimWhiteSpace(rxi);
          reportEvent("NMEA NOTE: " + rxi);
        }
      }
    }

    retractRunWarning("Failed to connect to server"); // If we're connected, we no longer need the warning
  } else {
    send_queue.clear();
    if (m_curr_time - m_last_nmea_connect_time >= attempt_reconnect_interval) {
      ConnectToNMEAServer();
    }
  }

  AppCastingMOOSApp::PostReport();
  return(true);
}

//---------------------------------------------------------
// Procedure: OnStartUp()
//            happens before connection is open

bool Neptune::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();

  STRING_LIST sParams;
  m_MissionReader.EnableVerbatimQuoting(false);
  if(!m_MissionReader.GetConfiguration(GetAppName(), sParams))
    reportConfigWarning("No config block found for " + GetAppName());

  STRING_LIST::iterator p;
  for(p=sParams.begin(); p!=sParams.end(); p++) {
    string orig  = *p;
    string line  = *p;
    string param = tolower(biteStringX(line, '='));
    string value = line;

    bool handled = false;
    if(param == "port") {
      if (isNumber(value)) {
        int port = stoi(value);
        if (port > 65535 || port < 1) {
          reportConfigWarning("Port " + value + " is outside valid port range 1-65535");
        } else {
          m_connect_port = stoi(value);
        }
      } else {
        reportConfigWarning("Unable to parse requested port " + value + " to int");
      }
      handled = true;
    } else if (param == "host") {
      m_connect_addr = value;
      handled = true;
    } else if (param == "reconnectinterval") {
      attempt_reconnect_interval = stod(value);
      handled = true;
    } else if (param == "validatechecksum") {
      if (!setBooleanOnString(validate_checksum, value)) {
        reportConfigWarning(param + " is not set to true or false, skipping");
      }
      handled = true;
    } else if (param == "maximumtimedifference" || param == "maximumtimedelta") {
      if (!setDoubleOnString(maximum_time_delta, value)) {
        reportConfigWarning(param + " is not a number, skipping");
      }
      handled = true;
    }

    if(!handled)
      reportUnhandledConfigWarning(orig);

  }

  double latOrigin = 0.0;
  double lonOrigin = 0.0;
  if (!(m_MissionReader.GetValue("LatOrigin", latOrigin) && m_MissionReader.GetValue("LongOrigin", lonOrigin)
        && (m_geo_initialized = m_geo.Initialise(latOrigin, lonOrigin)))) {
    reportRunWarning("Error calculating datum! XY Local Grid Unavaliable");
  }
  
  registerVariables();	
  return(true);
}

//---------------------------------------------------------
// Procedure: registerVariables

void Neptune::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();

  Register("NAV_SPEED", 0);
  Register("NAV_HEADING", 0);
  Register("NAV_DEPTH", 0);
  Register("NAV_LAT", 0);
  Register("NAV_LONG", 0);
  Register("NAV_ALTITUDE", 0);
}


//------------------------------------------------------------
// Procedure: buildReport()

bool Neptune::buildReport() 
{
  std::string host = "UNKNOWN";
  host = inet_ntoa(m_server.get_addr().sin_addr);
  if (!MOOSStrCmp(host, m_connect_addr)) {
    host = m_connect_addr + " (" + host + ")";
  }

  m_msgs << "Valid Connection: " << boolToString(m_server.is_valid()) << endl;
  m_msgs << "Host: " << host << endl;
  m_msgs << "Port: " << intToString(m_connect_port) << endl;

  return(true);
}