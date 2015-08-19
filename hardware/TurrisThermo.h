#pragma once

#include "DomoticzHardware.h"
#include <sensors/sensors.h>

class CTurrisThermo : public CDomoticzHardwareBase
{
public:
	CTurrisThermo(const int ID);
	~CTurrisThermo(void);

	bool WriteToHardware(const char *pdata, const unsigned char length);
private:
	volatile bool m_stoprequested;
	boost::shared_ptr<boost::thread> m_thread;
	enum {
		TURRIS_TH_INIT, TURRIS_TH_READING, TURRIS_TH_END
	} turrisThState;
	const sensors_chip_name* nptr;

	bool StartHardware();
	bool StopHardware();
	void Do_Work();
	void GetSensorDetails();
};
