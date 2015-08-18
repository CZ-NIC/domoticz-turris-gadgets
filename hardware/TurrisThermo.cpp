#include "TurrisThermo.h"
#include "../main/Helper.h"
#include "../main/Logger.h"
#include "hardwaretypes.h"
#include "../main/RFXtrx.h"
#include "../main/localtime_r.h"
#include "../main/mainworker.h"

#include <linux/i2c-dev.h>

#define TURRIS_THERMO_POLL_INTERVAL 30


#define I2C_LOCAL "/dev/i2c-0"
#define I2C_ADDRESS_7_THERMOMETER 0x4C

#define BUFFSIZE 64
#define HANDLE_ERROR() _log.Log(LOG_ERROR, "TurrisThermo %s\n", strerror(errno))

CTurrisThermo::CTurrisThermo(const int ID)
{
	m_HwdID=ID;
	m_stoprequested=false;
}

CTurrisThermo::~CTurrisThermo(void)
{
}

bool CTurrisThermo::StartHardware()
{
	//Start worker thread
	m_thread = boost::shared_ptr<boost::thread>(new boost::thread(boost::bind(&CTurrisThermo::Do_Work, this)));
	m_bIsStarted=true;
	sOnConnected(this);

	return (m_thread!=NULL);
}

bool CTurrisThermo::StopHardware()
{
	if (m_thread!=NULL)
	{
		assert(m_thread);
		m_stoprequested = true;
		m_thread->join();
	}
	m_bIsStarted=false;
    return true;
}

void CTurrisThermo::Do_Work()
{
	int sec_counter = TURRIS_THERMO_POLL_INTERVAL - 5;
	_log.Log(LOG_STATUS,"TurrisThermo: Worker started...");
	while (!m_stoprequested)
	{
		sleep_seconds(1);
		sec_counter++;
		if ((sec_counter % 12) == 0)
		{
			m_LastHeartbeat = mytime(NULL);
		}

		if ((sec_counter % TURRIS_THERMO_POLL_INTERVAL) == 0)
		{
			GetSensorDetails();
		}
	}
	_log.Log(LOG_STATUS,"TurrisThermo: Worker stopped...");
}

bool CTurrisThermo::WriteToHardware(const char *pdata, const unsigned char length)
{
	return false;
}

void CTurrisThermo::GetSensorDetails()
{
	const char *path = I2C_LOCAL;
	int fd;

	fd = open(path, O_RDWR);
	if (fd < 0) {
		_log.Log(LOG_ERROR, "TurrisThermo: Cannot open device \"%s\" (%s)", path, strerror(errno));
		return;
	}
	if (ioctl(fd, I2C_SLAVE, I2C_ADDRESS_7_THERMOMETER) < 0) {
		HANDLE_ERROR();
		return;
	}

	//Prepare data
	char buff[BUFFSIZE];
	//Read local temperature
	buff[0] = 0x00;
	if (write(fd, buff, 1) != 1) {
		HANDLE_ERROR();
		return;
	}
	if (read(fd, buff, 1) != 1) {
		HANDLE_ERROR();
		return;
	} else {
		SendTempSensor(1, 100, (float)(buff[0]), "Turris Board Temp");
	}
	//Read remote temperature
	buff[0] = 0x01;
	if (write(fd, buff, 1) != 1) {
		HANDLE_ERROR();
		return;
	}
	if (read(fd, buff, 1) != 1) {
		HANDLE_ERROR();
		return;
	} else {
		SendTempSensor(0, 100, (float)(buff[0]), "Turris CPU Temp");
	}
}
