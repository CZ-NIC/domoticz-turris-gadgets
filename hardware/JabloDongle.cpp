#include "stdafx.h"
#include "JabloDongle.h"
#include "../main/Helper.h"
#include "../main/Logger.h"
#include "hardwaretypes.h"
#include "../main/RFXtrx.h"
#include "../main/SQLHelper.h"
#include "../main/localtime_r.h"
#include "../main/mainworker.h"

CJabloDongle::CJabloDongle(const int ID, const std::string& devname, unsigned int baud_rate)
{
	m_HwdID=ID;
	m_bSkipReceiveCheck = true;
	m_stoprequested = false;
	m_bufferpos = 0;
	memset(m_buffer, 0, sizeof(m_buffer));
	m_szSerialPort = devname;
	m_iBaudRate = baud_rate;
	last_mid = -1;
	last_mtype = JMTYPE_UNDEF;
	slots.clear();
}

CJabloDongle::~CJabloDongle(void)
{
	m_bIsStarted=false;
}

int CJabloDongle::ProbeDongle(void) {
	boost::unique_lock<boost::mutex> probeLock(probeMut);

	writeString("\nWHO AM I?\n");

	boost::system_time timeout = boost::get_system_time() + boost::posix_time::milliseconds(1000);
	if(!probeCond.timed_wait(probeLock, timeout)) {
		//Vyhucelo na timeout
		_log.Log(LOG_ERROR, "Turris Dongle not found on port %s!", m_szSerialPort.c_str());
		return -1;
	}

	return 0;
}

bool CJabloDongle::StartHardware()
{
	m_thread = boost::shared_ptr<boost::thread>(new boost::thread(boost::bind(&CJabloDongle::Do_Work, this)));
	return (m_thread!=NULL);
}

bool CJabloDongle::StopHardware()
{
	if (m_thread!=NULL)
	{
		assert(m_thread);
		m_stoprequested = true;
		m_thread->join();
	}
	m_bIsStarted=false;

	//slots array cleanup
	for(std::vector<Ja_device*>::iterator i = slots.begin(); i != slots.end(); i++) {
		delete *i;
	}

    return true;
}

void CJabloDongle::Do_Work() {
	int sec_counter=30-5;
	_log.Log(LOG_STATUS,"JabloDongle: Worker started...");
	while (!m_stoprequested)
	{
		sleep_seconds(1);
		sec_counter++;
		if (sec_counter%12==0)
		{
			m_LastHeartbeat=mytime(NULL);
		}

		if (sec_counter%30==0)
		{
			//poll status
		}

		if(!isOpen()) {
			OpenSerialDevice();
			if(ProbeDongle()) {
				m_stoprequested = true;
			}
			ReadSlots();
			SendSwitchIfNotExists(0x1000000, SUBSWITCH_PGX, 0, false, 0, "PGX");
			SendSwitchIfNotExists(0x1000000, SUBSWITCH_PGY, 0, false, 0, "PGY");
			if(!SendSwitchIfNotExists(0x1000001, 0, 0, false, 0, "SIREN_LOUD")) {
				SetSwitchIcon(0x1000001, 0, 13); //Set wonderful red light icon in web gui
			}
			if(!SendSwitchIfNotExists(0x1000002, 0, 0, false, 0, "SIREN_BEEP")) {
				SetSwitchType(0x1000002, 0, STYPE_Dimmer);
			}
			if(!SendSwitchIfNotExists(0x1000003, 0, 0, false, 0, "ENROLL")) {
				SetSwitchIcon(0x1000003, 0, 9);
			}

			//add smoke detectors at startup (they are difficult to activate :-) )
			for(std::vector<Ja_device*>::iterator i = slots.begin(); i != slots.end(); i++) {
				if((*i)->model == JDEV_JA85ST) {
					std::stringstream dev_desc;
					dev_desc << (*i)->ModelAsString() << "_" << std::setfill('0') << (*i)->id << "_SENSOR";
					SendSwitchIfNotExists((*i)->id, SUBSWITCH_SENSOR, 0, false, 0, dev_desc.str());
				}
			}
		}
	}
	_log.Log(LOG_STATUS,"JabloDongle: Worker stopped...");
}

bool CJabloDongle::OpenSerialDevice()
{
	try
	{
		_log.Log(LOG_STATUS, "JabloDongle: Using serial port %s", m_szSerialPort.c_str());
		open(m_szSerialPort, 57600);
	}
	catch (boost::exception & e)
	{
		_log.Log(LOG_ERROR, "JabloDongle: Error opening serial port!");
		return false;
	}
	catch (...)
	{
		_log.Log(LOG_ERROR, "JabloDongle: Error opening serial port!!!");
		return false;
	}
	m_bIsStarted = true;
	boost::this_thread::sleep(boost::posix_time::milliseconds(100));
	setReadCallback(boost::bind(&CJabloDongle::ReadCallback, this, _1, _2));
	sOnConnected(this);
	return true;
}

void CJabloDongle::ProbeCallback(std::string version) {
	_log.Log(LOG_STATUS, "Turris Dongle on port %s, FW version %s", m_szSerialPort.c_str(), version.c_str());
}

void CJabloDongle::ReadSlots(void) {
	std::stringstream req;
	int slot_no;
	int read_attempts;

	boost::unique_lock<boost::mutex> readSlotsLock(readSlotsMut);

	_log.Log(LOG_STATUS, "Reading learned devices:");

	for(slot_no = 0; slot_no < 32; slot_no++) {
		read_attempts = 3;
		while(read_attempts--) {
			req.str(std::string());
			req << "\nGET SLOT:" << std::setfill('0') << std::setw(2) << slot_no << "\n";
			writeString(req.str());

			boost::system_time timeout = boost::get_system_time() + boost::posix_time::milliseconds(200);

			if(readSlotsCond.timed_wait(readSlotsLock, timeout)) {
				break;
			}
			else {
				_log.Log(LOG_STATUS, "Slot %d read timed out!", slot_no);
				if(read_attempts == 0) {
					_log.Log(LOG_STATUS, "Slot read error!");
					return;
				}
			}
		}

	}

	_log.Log(LOG_STATUS, "Slots read finished!");
}

void CJabloDongle::SlotReadCallback(unsigned int slot_no, unsigned int slot_val) {
	Ja_device *jdev;

	if(slot_val != 0) {
		jdev = new Ja_device(slot_val);
		slots.push_back(jdev);
		_log.Log(LOG_STATUS, "Slot %d: dev %d (%s)", slot_no, slot_val, jdev->ModelAsString().c_str());
	}
}

JaMessage CJabloDongle::ParseMessage(std::string msgstring) {
	JaMessage msg;
	std::stringstream msgstream(msgstring);
	std::string msgtok;
	int tokNum;

	if(msgstring == "\nOK\n") {
		msg.mtype = JMTYPE_OK;
	}
	else if(msgstring == "\nERROR\n") {
		msg.mtype = JMTYPE_ERR;
	}
	else if(!msgstring.compare(0, 16, "\nTURRIS DONGLE V") && (msgstring.length() > 17)) {
		msg.mtype = JMTYPE_VERSION;
		msg.version = std::string(msgstring.c_str() + 16);
		msg.version.erase(msg.version.size() - 1);
	}
	else if(!msgstring.compare(0, 6, "\nSLOT:")) {
		if(sscanf(msgstring.c_str(), "\nSLOT:%u [%u]\n", &msg.slot_num, &msg.slot_val) == 2) {
			msg.mtype = JMTYPE_SLOT;
		}
		else if(sscanf(msgstring.c_str(), "\nSLOT:%u [--------]\n", &msg.slot_num) == 1) {
			msg.mtype = JMTYPE_SLOT;
			msg.slot_val = 0;
		}
	}
	else {
		tokNum = 0;
		while(msgstream >> msgtok) {
			if(tokNum == 0) {
				if(sscanf(msgtok.c_str(), "[%u]", &msg.did) != 1)
					break;
			}
#ifdef OLDFW
			else if(tokNum == 1) {
				if(sscanf(msgtok.c_str(), "ID:%u", &msg.mid) != 1) {
					msg.mid = -1;
					if(msgtok.compare("ID:---") != 0)
						break;
				}
			}
#endif
#ifdef OLDFW
			else if(tokNum == 2) {
#else
			else if(tokNum == 1) {
#endif
				msg.devmodel = msgtok;
			}
#ifdef OLDFW
			else if(tokNum == 3) {
#else
			else if(tokNum == 2) {
#endif
				if(msgtok == "SENSOR") {
					msg.mtype = JMTYPE_SENSOR;
				}
				else if(msgtok == "TAMPER") {
					msg.mtype = JMTYPE_TAMPER;
				}
				else if(msgtok == "BEACON") {
					msg.mtype = JMTYPE_BEACON;
				}
				else if(msgtok == "BUTTON") {
					msg.mtype = JMTYPE_BUTTON;
				}
				else if(msgtok == "ARM:1") {
					msg.mtype = JMTYPE_ARM;
				}
				else if(msgtok == "ARM:0") {
					msg.mtype = JMTYPE_DISARM;
				}
				else if(sscanf(msgtok.c_str(), "SET:%f", &msg.temp) == 1) {
					msg.mtype = JMTYPE_SET;
				}
				else if(sscanf(msgtok.c_str(), "INT:%f", &msg.temp) == 1) {
					msg.mtype = JMTYPE_INT;
				}
				else {
					msg.mtype = JMTYPE_UNDEF;
				}
			}
			else {
				if(sscanf(msgtok.c_str(), "LB:%d", &msg.lb) != 1)
					if(sscanf(msgtok.c_str(), "ACT:%d", &msg.act) != 1)
						sscanf(msgtok.c_str(), "BLACKOUT:%d", &msg.blackout);
			}

			tokNum++;
		}
	}

	return msg;
}

void CJabloDongle::ProcessMessage(JaMessage jmsg) {
	Ja_device *jdev;

	if((jmsg.mtype != JMTYPE_SLOT) && (jmsg.mtype != JMTYPE_VERSION) && (jmsg.mtype != JMTYPE_OK) && (jmsg.mtype != JMTYPE_ERR)) {
		_log.Log(LOG_STATUS, "Received message of type %s from device %d (%s)", jmsg.MtypeAsString().c_str(), jmsg.did, jmsg.devmodel.c_str());
	}

	switch(jmsg.mtype) {
		case JMTYPE_SLOT: {
			SlotReadCallback(jmsg.slot_num, jmsg.slot_val);
			readSlotsCond.notify_one();
			break;
		}
		case JMTYPE_VERSION: {
			ProbeCallback(jmsg.version);
			probeCond.notify_one();
			break;
		}
		case JMTYPE_SENSOR: {
				std::stringstream dev_desc;
				dev_desc << jmsg.devmodel << "_" << std::setfill('0') << jmsg.did << "_SENSOR";
				SendSwitch(jmsg.did, SUBSWITCH_SENSOR, (jmsg.lb == 1) ? 0 : 100, (jmsg.act == -1) ? 1 : jmsg.act, 0, dev_desc.str());
			break;
		}
		case JMTYPE_TAMPER: {
				std::stringstream dev_desc;
				dev_desc << jmsg.devmodel << "_" << std::setfill('0') << jmsg.did << "_TAMPER";
				SendSwitch(jmsg.did, SUBSWITCH_TAMPER, (jmsg.lb == 1) ? 0 : 100, (jmsg.act == -1) ? 1 : jmsg.act, 0, dev_desc.str());

			break;
		}
		case JMTYPE_BUTTON: {
				std::stringstream dev_desc;
				dev_desc << jmsg.devmodel << "_" << std::setfill('0') << jmsg.did << "_BUTTON";
				SendSwitch(jmsg.did, SUBSWITCH_BUTTON, (jmsg.lb == 1) ? 0 : 100, (jmsg.act == -1) ? 1 : jmsg.act, 0, dev_desc.str());

			break;
		}
		case JMTYPE_ARM:
		case JMTYPE_DISARM: {
				std::stringstream dev_desc;
				dev_desc << jmsg.devmodel << "_" << std::setfill('0') << jmsg.did << "_ARM";
				SendSwitch(jmsg.did, SUBSWITCH_ARM, (jmsg.lb == 1) ? 0 : 100, (jmsg.mtype == JMTYPE_ARM) ? 1 : 0, 0, dev_desc.str());

			break;
		}
		case JMTYPE_SET: {
			std::stringstream dev_desc;
			dev_desc << jmsg.devmodel << "_" << std::setfill('0') << jmsg.did << "_SET";

			SendSetPointSensor(jmsg.did, (jmsg.lb == 1) ? 0 : 100, jmsg.temp, dev_desc.str());

			break;
		}
		case JMTYPE_INT: {
			std::stringstream dev_desc;
			dev_desc << jmsg.devmodel << "_" << std::setfill('0') << jmsg.did << "_INT";
			SendTempSensor(jmsg.did, (jmsg.lb == 1) ? 0 : 100, jmsg.temp, dev_desc.str());
			break;
		}
	}
}

void CJabloDongle::ReadCallback(const char *data, size_t len)
{
	unsigned char *mf, *ml;
	unsigned char *bufptr;
	unsigned char msgline[128];
	JaMessage jmsg;
	bool messagesInBuffer;

	boost::lock_guard<boost::mutex> l(readQueueMutex);

	if (!m_bIsStarted)
		return;

	if (!m_bEnableReceive)
		return;

	//receive data to buffer
	if((m_bufferpos + len) < sizeof(m_buffer)) {
		memcpy(m_buffer + m_bufferpos, data, len);
		m_bufferpos += len;
	}
	else {
		_log.Log(LOG_STATUS, "JabloDongle: Buffer Full");
	}

	//m_buffer[m_bufferpos] = '\0';
	//_log.Log(LOG_STATUS, "Pokus received: %s", m_buffer);

	do {
		messagesInBuffer = false;
		//find sync sequence \n[
		bufptr = m_buffer;
		while((mf = (unsigned char*)strchr((const char*)bufptr, '\n')) != NULL) {
			//check if character after newline is printable character
			if((mf[1] > 32 && (mf[1] < 127)))
				break;
			bufptr = mf + 1;
		}

		//is there at least 1 whole msg in buffer?
		if((mf != NULL) && (strlen((char*)mf) > 2)) {
			ml = (unsigned char*)strchr((char*)mf + 2, '\n');
			if(ml != NULL)
				messagesInBuffer = true;
		}

		if(messagesInBuffer) {
			//copy single message into separate buffer
			memcpy(msgline, mf, ml - mf + 1);
			msgline[ml - mf + 1] = '\0';

			//shift message buffer and adjust end pointer
			memmove(m_buffer, ml + 1, m_bufferpos);
			m_bufferpos -= (ml - m_buffer) + 1;

			//process message
			//_log.Log(LOG_STATUS, "Received line %s", msgline);

			jmsg = ParseMessage(std::string((char*)msgline));

#ifdef OLDFW
			//quick and dirty hack for msg deduplication, will be removed in final version
			if((jmsg.mid == -1) && ((jmsg.mtype != last_mtype) || ((jmsg.mtype != JMTYPE_SET) && (jmsg.mtype != JMTYPE_INT)))) {
				ProcessMessage(jmsg);
				last_mtype = jmsg.mtype;
			}
			else if(jmsg.mid != last_mid) {
				ProcessMessage(jmsg);
				last_mid = jmsg.mid;
			}
#else
			ProcessMessage(jmsg);
#endif

		}
	}while(messagesInBuffer);
}

void CJabloDongle::SendTempSensor(int NodeID, const int BatteryLevel, const float temperature, const std::string &defaultname)
{
	bool bDeviceExits = true;
	std::stringstream szQuery;
	std::vector<std::vector<std::string> > result;

	NodeID &= 0xFFFF; //TEMP packet has only 2 bytes for ID.

	char szTmp[30];
	sprintf(szTmp, "%d", (unsigned int)NodeID);

	szQuery << "SELECT Name FROM DeviceStatus WHERE (HardwareID==" << m_HwdID << ") AND (DeviceID=='" << szTmp << "') AND (Type==" << int(pTypeTEMP) << ") AND (Subtype==" << int(sTypeTemperature) << ")";
	result = m_sql.query(szQuery.str());
	if (result.size() < 1)
	{
		bDeviceExits = false;
	}

	RBUF tsen;
	memset(&tsen, 0, sizeof(RBUF));
	tsen.TEMP.packetlength = sizeof(tsen.TEMP) - 1;
	tsen.TEMP.packettype = pTypeTEMP;
	tsen.TEMP.subtype = sTypeTemperature;
	tsen.TEMP.battery_level = BatteryLevel;
	tsen.TEMP.rssi = 12;
	tsen.TEMP.id1 = (NodeID & 0xFF00) >> 8;
	tsen.TEMP.id2 = NodeID & 0xFF;
	tsen.TEMP.tempsign = (temperature >= 0) ? 0 : 1;
	int at10 = round(temperature*10.0f);
	tsen.TEMP.temperatureh = (BYTE)(at10 / 256);
	at10 -= (tsen.TEMP.temperatureh * 256);
	tsen.TEMP.temperaturel = (BYTE)(at10);
	sDecodeRXMessage(this, (const unsigned char *)&tsen.TEMP);

	if (!bDeviceExits)
	{
		//Assign default name for device
		szQuery.clear();
		szQuery.str("");
		szQuery << "UPDATE DeviceStatus SET Name='" << defaultname << "' WHERE (HardwareID==" << m_HwdID << ") AND (DeviceID=='" << szTmp << "') AND (Type==" << int(pTypeTEMP) << ") AND (Subtype==" << int(sTypeTemperature) << ")";
		m_sql.query(szQuery.str());
	}
}

int CJabloDongle::SendSwitchIfNotExists(const int NodeID, const int ChildID, const int BatteryLevel, const bool bOn, const double Level, const std::string &defaultname)
{
	//make device ID
	unsigned char ID1 = (unsigned char)((NodeID & 0xFF000000) >> 24);
	unsigned char ID2 = (unsigned char)((NodeID & 0xFF0000) >> 16);
	unsigned char ID3 = (unsigned char)((NodeID & 0xFF00) >> 8);
	unsigned char ID4 = (unsigned char)NodeID & 0xFF;

	char szIdx[10];
	sprintf(szIdx, "%X%02X%02X%02X", ID1, ID2, ID3, ID4);
	std::stringstream szQuery;
	std::vector<std::vector<std::string> > result;
	szQuery << "SELECT Name,nValue,sValue FROM DeviceStatus WHERE (HardwareID==" << m_HwdID << ") AND (DeviceID=='" << szIdx << "') AND (Unit == " << ChildID << ") AND (Type==" << int(pTypeLighting2) << ") AND (Subtype==" << int(sTypeAC) << ")";
	result = m_sql.query(szQuery.str()); //-V519
	if (result.size() < 1)
	{
		SendSwitch(NodeID, ChildID, BatteryLevel, bOn, Level, defaultname);
		return 0;
	}

	return 1;
}

void CJabloDongle::SetSwitchType(const int NodeID, const int ChildID, enum _eSwitchType switchType) {
	std::stringstream szQuery;

	unsigned char ID1 = (unsigned char)((NodeID & 0xFF000000) >> 24);
	unsigned char ID2 = (unsigned char)((NodeID & 0xFF0000) >> 16);
	unsigned char ID3 = (unsigned char)((NodeID & 0xFF00) >> 8);
	unsigned char ID4 = (unsigned char)NodeID & 0xFF;

	char szIdx[10];
	sprintf(szIdx, "%X%02X%02X%02X", ID1, ID2, ID3, ID4);

	szQuery.clear();
	szQuery.str("");
	szQuery << "UPDATE DeviceStatus SET SwitchType=" << int(switchType) << " WHERE (HardwareID==" << m_HwdID << ") AND (DeviceID=='" << szIdx << "') AND (Unit == " << ChildID << ") AND (Type==" << int(pTypeLighting2) << ") AND (Subtype==" << int(sTypeAC) << ")";
	m_sql.query(szQuery.str());
}

void CJabloDongle::SetSwitchIcon(const int NodeID, const int ChildID, const int iconIdx) {
	std::stringstream szQuery;

	unsigned char ID1 = (unsigned char)((NodeID & 0xFF000000) >> 24);
	unsigned char ID2 = (unsigned char)((NodeID & 0xFF0000) >> 16);
	unsigned char ID3 = (unsigned char)((NodeID & 0xFF00) >> 8);
	unsigned char ID4 = (unsigned char)NodeID & 0xFF;

	char szIdx[10];
	sprintf(szIdx, "%X%02X%02X%02X", ID1, ID2, ID3, ID4);

	szQuery.clear();
	szQuery.str("");
	szQuery << "UPDATE DeviceStatus SET CustomImage=" << iconIdx << " WHERE (HardwareID==" << m_HwdID << ") AND (DeviceID=='" << szIdx << "') AND (Unit == " << ChildID << ") AND (Type==" << int(pTypeLighting2) << ") AND (Subtype==" << int(sTypeAC) << ")";
	m_sql.query(szQuery.str());
}

void CJabloDongle::SendSetPointSensor(int NodeID, const int BatteryLevel, const float Temp, const std::string &defaultname)
{
	bool bDeviceExits=true;
	std::stringstream szQuery;

	NodeID &= 0xFFFF; //To be consistent with TEMP packet, which has only 2 bytes for ID.

	//make device ID
	unsigned char ID1 = (unsigned char)((NodeID & 0xFF000000) >> 24);
	unsigned char ID2 = (unsigned char)((NodeID & 0xFF0000) >> 16);
	unsigned char ID3 = (unsigned char)((NodeID & 0xFF00) >> 8);
	unsigned char ID4 = (unsigned char)NodeID & 0xFF;

	char szIdx[30];
		sprintf(szIdx, "%X%02X%02X%02X", ID1, ID2, ID3, ID4);

	std::vector<std::vector<std::string> > result;
	szQuery << "SELECT Name FROM DeviceStatus WHERE (HardwareID==" << m_HwdID << ") AND (DeviceID=='" << szIdx << "')";
	result=m_sql.query(szQuery.str());
	if (result.size()<1)
	{
		bDeviceExits=false;
	}

	_tThermostat thermos;
	thermos.subtype=sTypeThermSetpoint;
	thermos.id1=ID1;
	thermos.id2=ID2;
	thermos.id3=ID3;
	thermos.id4=ID4;
	thermos.dunit=0;
	thermos.battery_level = (unsigned char)BatteryLevel;
	thermos.temp=Temp;

	sDecodeRXMessage(this, (const unsigned char *)&thermos);

	if (!bDeviceExits)
	{
		//Assign default name for device
		szQuery.clear();
		szQuery.str("");
		szQuery << "UPDATE DeviceStatus SET Name='" << defaultname << "' WHERE (HardwareID==" << m_HwdID << ") AND (DeviceID=='" << szIdx << "')";
		result=m_sql.query(szQuery.str());
	}
}

bool CJabloDongle::WriteToHardware(const char *pdata, const unsigned char length)
{
	unsigned int id;
	bool retval;
	_log.Log(LOG_STATUS, "JabloDongle: WriteToHardware");

	tRBUF *tsen = (tRBUF*) pdata;
	_log.Log(LOG_STATUS, "packet type = 0x%x", tsen->LIGHTING2.packettype);

	if(tsen->LIGHTING2.packettype != pTypeLighting2) {
		//unknown hardware type
		return false;
	}

	id = (tsen->LIGHTING2.id1 << 24) | (tsen->LIGHTING2.id2 << 16) | (tsen->LIGHTING2.id3 << 8) | (tsen->LIGHTING2.id4);
	_log.Log(LOG_STATUS, "id = %d, cmnd = %d, level = %d", id, tsen->LIGHTING2.cmnd, tsen->LIGHTING2.level);

	retval = false;

	if(id == 0x1000000) { //ID of PGX and PGY switches
		switch(tsen->LIGHTING2.cmnd) {
			case light2_sOn:
			case light2_sGroupOn: {
				if(tsen->LIGHTING2.unitcode == SUBSWITCH_PGX)
					txState.pgx = 1;
				else if(tsen->LIGHTING2.unitcode == SUBSWITCH_PGY)
					txState.pgy = 1;

				TransmitState();
				break;
			}
			case light2_sOff:
			case light2_sGroupOff: {
				if(tsen->LIGHTING2.unitcode == SUBSWITCH_PGX)
					txState.pgx = 0;
				else if(tsen->LIGHTING2.unitcode == SUBSWITCH_PGY)
					txState.pgy = 0;

				TransmitState();
				break;
			}
		}
		retval = true;
	}
	else if(id == 0x1000001) { //ID of siren (loud sound)
		switch(tsen->LIGHTING2.cmnd) {
			case light2_sOn:
			case light2_sGroupOn: {
				txState.alarm = 1;
				TransmitState();
				break;
			}
			case light2_sOff:
			case light2_sGroupOff: {
				txState.alarm = 0;
				TransmitState();
				break;
			}
		}
		retval = true;
	}
	else if(id == 0x1000002) { //ID of siren (beep), implemented as dimmer. Field 'level' is in range from 0 to 14 (0 to 100%)
		switch(tsen->LIGHTING2.cmnd) {
			case light2_sOn:
			case light2_sGroupOn: {
				txState.beep = txState.lastBeepState;
				TransmitState();
				break;
			}
			case light2_sOff:
			case light2_sGroupOff: {
				txState.lastBeepState = txState.beep;
				txState.beep = JA_BEEP_NONE;
				TransmitState();
				break;
			}
			case light2_sSetLevel:
			case light2_sSetGroupLevel: {
				if(tsen->LIGHTING2.level == 0) {
					txState.lastBeepState = txState.beep;
					txState.beep = JA_BEEP_NONE;
				}
				else if((tsen->LIGHTING2.level >= 1) && (tsen->LIGHTING2.level <= 7)) {
					txState.beep = JA_BEEP_SLOW;
				}
				else {
					txState.beep = JA_BEEP_FAST;
				}
				TransmitState();
				break;
			}
		}
		retval = true;
	}
	else if(id == 0x1000003) { //ID of ENROLL switch
		switch(tsen->LIGHTING2.cmnd) {
			case light2_sOn:
			case light2_sGroupOn: {
				txState.enroll = 1;
				TransmitState();
				break;
			}
			case light2_sOff:
			case light2_sGroupOff: {
				txState.enroll = 0;
				TransmitState();
				break;
			}
		}
		retval = true;
	}
	else { //ID of a real sensor
		if(((Ja_device::ModelFromID(id) == JDEV_JA83P) || (Ja_device::ModelFromID(id) == JDEV_JA82SH) || (Ja_device::ModelFromID(id) == JDEV_JA85ST)) && (tsen->LIGHTING2.unitcode == SUBSWITCH_SENSOR)) {
			//enable resetting status of momentary sensors by user
			switch(tsen->LIGHTING2.cmnd) {
				case light2_sOff:
				case light2_sGroupOff: {
					retval = true;
					break;
				}
			}
		}
		else if((Ja_device::ModelFromID(id) == JDEV_JA80L) && (tsen->LIGHTING2.unitcode == SUBSWITCH_TAMPER)) {
			//enable resetting status of JA-80L's momentary tamper by user
			switch(tsen->LIGHTING2.cmnd) {
				case light2_sOff:
				case light2_sGroupOff: {
					retval = true;
					break;
				}
			}
		}
		else if(((Ja_device::ModelFromID(id) == JDEV_JA80L) || (Ja_device::ModelFromID(id) == JDEV_JA85ST)) && (tsen->LIGHTING2.unitcode == SUBSWITCH_BUTTON)) {
			//enable resetting status of 'button' momentary subdevices by user
			switch(tsen->LIGHTING2.cmnd) {
				case light2_sOff:
				case light2_sGroupOff: {
					retval = true;
					break;
				}
			}
		}
		else if((Ja_device::ModelFromID(id) == JDEV_RC86K) && (tsen->LIGHTING2.unitcode == SUBSWITCH_ARM)) {
			//enable setting and resetting status of keychain switch by user
			switch(tsen->LIGHTING2.cmnd) {
				case light2_sOn:
				case light2_sGroupOn:
				case light2_sOff:
				case light2_sGroupOff: {
					retval = true;
					break;
				}
			}
		}
	}

	return retval;
}

void CJabloDongle::TransmitState(void) {
	const char* beep_types[] = {"NONE", "FAST", "SLOW"};
	char txbuf[64];
	snprintf(txbuf, 47, "\nTX ENROLL:%d PGX:%d PGY:%d ALARM:%d BEEP:%s\n",
			txState.enroll, txState.pgx, txState.pgy, txState.alarm, beep_types[txState.beep]);

	writeString(std::string(txbuf));
}

// ------------------------------------------------------------------------------------------------
#ifdef OLDFW
JaMessage::JaMessage() : did(0), mid(-1), devmodel("Unknown"), mtype(JMTYPE_UNDEF), lb(-1), act(-1), blackout(-1), slot_num(0), slot_val(0), temp(0) {
}
#else
JaMessage::JaMessage() : did(0), devmodel("Unknown"), mtype(JMTYPE_UNDEF), lb(-1), act(-1), blackout(-1), slot_num(0), slot_val(0), temp(0) {
}
#endif

std::string JaMessage::MtypeAsString() {
	const char* names[] = {"UNDEF", "ARM", "DISARM", "BEACON", "SENSOR", "TAMPER", "PANIC", "DEFECT", "BUTTON", "SET", "INT", "OK", "ERR", "SLOT", "VERSION"};
	return std::string(names[mtype]);
}

// ------------------------------------------------------------------------------------------------
Ja_device::Ja_device(unsigned int id) {
	this->id = id;
	SetModelFromID(id);
}

void Ja_device::SetModelFromID(unsigned int id) {
	model = ModelFromID(id);
}

enum ja_devmodel Ja_device::ModelFromID(unsigned int id) {
	if(id >= 0x800000 && id <= 0x87FFFF)
		return JDEV_RC86K;
	else if(id >= 0x900000 && id <= 0x97FFFF)
		return JDEV_RC86K;
	else if(id >= 0x180000 && id <= 0x1BFFFF)
		return JDEV_JA81M;
	else if(id >= 0x1C0000 && id <= 0x1DFFFF)
		return JDEV_JA83M;
	else if(id >= 0x640000 && id <= 0x65FFFF)
		return JDEV_JA83P;
	else if(id >= 0x7F0000 && id <= 0x7FFFFF)
		return JDEV_JA82SH;
	else if(id >= 0x760000 && id <= 0x76FFFF)
		return JDEV_JA85ST;
	else if(id >= 0x580000 && id <= 0x59FFFF)
		return JDEV_JA80L;
	else if(id >= 0xCF0000 && id <= 0xCFFFFF)
		return JDEV_AC88;
	else if(id >= 0x240000 && id <= 0x25FFFF)
		return JDEV_TP82N;

	return JDEV_UNKNOWN;
}

std::string Ja_device::ModelAsString() {
	const char* names[] = { "UNKNOWN", "RC-86K", "JA-83P", "JA-81M", "JA-83M", "JA-82SH", "JA-85ST", "TP-82N", "JA-80L", "AC-88" };
	return std::string(names[model]);
}
