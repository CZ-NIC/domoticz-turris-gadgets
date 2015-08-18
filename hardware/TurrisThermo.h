#pragma once

#include "DomoticzHardware.h"

class CTurrisThermo : public CDomoticzHardwareBase
{
public:
	CTurrisThermo(const int ID);
	~CTurrisThermo(void);

	bool WriteToHardware(const char *pdata, const unsigned char length);
private:
	volatile bool m_stoprequested;
	boost::shared_ptr<boost::thread> m_thread;

	bool StartHardware();
	bool StopHardware();
	void Do_Work();
	void GetSensorDetails();
};
