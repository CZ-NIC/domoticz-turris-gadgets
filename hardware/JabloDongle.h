#pragma once

#include "DomoticzHardware.h"
#include "ASyncSerial.h"
#include <iostream>

//#define OLDFW

#define SUBSWITCH_SENSOR	0
#define SUBSWITCH_TAMPER	1
#define SUBSWITCH_BUTTON	2

#define SUBSWITCH_PGX	0
#define SUBSWITCH_PGY	1

#define SUBSWITCH_ARM	0

class Ja_device;

enum ja80l_beep {
	JA_BEEP_NONE = 0,
	JA_BEEP_FAST = 1,
	JA_BEEP_SLOW = 2
};

struct tx_state_s {
	int enroll;
	int pgx;
	int pgy;
	int alarm;
	enum ja80l_beep beep;
	enum ja80l_beep lastBeepState;

	tx_state_s() : enroll(0), pgx(0), pgy(0), alarm(0), beep(JA_BEEP_NONE), lastBeepState(JA_BEEP_SLOW) {
	};
};

enum ja_devmodel {
	JDEV_UNKNOWN,
	JDEV_RC86K,
	JDEV_JA83P,
	JDEV_JA81M,
	JDEV_JA83M,
	JDEV_JA82SH,
	JDEV_JA85ST,
	JDEV_TP82N,
	JDEV_JA80L,
	JDEV_AC88
};

enum ja_mtype {
	JMTYPE_UNDEF = 0,
	JMTYPE_ARM,
	JMTYPE_DISARM,
	JMTYPE_BEACON,
	JMTYPE_SENSOR,
	JMTYPE_TAMPER,
	JMTYPE_PANIC,
	JMTYPE_DEFECT,
	JMTYPE_BUTTON,
	JMTYPE_SET,
	JMTYPE_INT,
	JMTYPE_OK,
	JMTYPE_ERR,
	JMTYPE_SLOT,
	JMTYPE_VERSION
};

class JaMessage {
public:
	unsigned int did;
#ifdef OLDFW
	int mid;
#endif
	std::string devmodel;
	enum ja_mtype mtype;
	int act, lb, blackout;
	float temp;
	unsigned int slot_num, slot_val;
	std::string version;
	JaMessage();
	std::string MtypeAsString();
};

class CJabloDongle : public AsyncSerial, public CDomoticzHardwareBase
{
public:
	CJabloDongle(const int ID, const std::string& devname, unsigned int baud_rate);
	~CJabloDongle(void);
	std::string m_szSerialPort;
	unsigned int m_iBaudRate;
	bool WriteToHardware(const char *pdata, const unsigned char length);
private:
	volatile bool m_stoprequested;
	boost::shared_ptr<boost::thread> m_thread;

	bool StartHardware();
	bool StopHardware();
	void Do_Work();
	int SendSwitchIfNotExists(const int NodeID, const int ChildID, const int BatteryLevel, const bool bOn, const double Level, const std::string &defaultname);
	void SendTempSensor(int NodeID, const int BatteryLevel, const float temperature, const std::string &defaultname);
	void SendSetPointSensor(int NodeID, const int BatteryLevel, const float Temp, const std::string &defaultname);
	void SetSwitchType(const int NodeID, const int ChildID, enum _eSwitchType switchType);
	void SetSwitchIcon(const int NodeID, const int ChildID, const int iconIdx);

	bool OpenSerialDevice();
	void ReadCallback(const char *data, size_t len);
	JaMessage ParseMessage(std::string msgstring);
	void ProcessMessage(JaMessage jmsg);
	void TransmitState(void);

	int ProbeDongle(void);
	void ProbeCallback(std::string version);
	boost::mutex probeMut;
	boost::condition_variable probeCond;

	void ReadSlots(void);
	void SlotReadCallback(unsigned int slot_no, unsigned int slot_val);
	boost::mutex readSlotsMut;
	boost::condition_variable readSlotsCond;

	int last_mid; ja_mtype last_mtype; //TODO remove
	std::vector<Ja_device*> slots;
	unsigned char m_buffer[16384];
	int m_bufferpos;
	struct tx_state_s txState;
};

class Ja_device {
public:
	unsigned int id;
	enum ja_devmodel model;
	Ja_device(unsigned int id);
	std::string ModelAsString();
	static enum ja_devmodel ModelFromID(unsigned int id);
private:
	void SetModelFromID(unsigned int id);
};
