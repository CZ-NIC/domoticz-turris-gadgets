#include "TurrisThermo.h"
#include "../main/Helper.h"
#include "../main/Logger.h"
#include "hardwaretypes.h"
#include "../main/RFXtrx.h"
#include "../main/localtime_r.h"
#include "../main/mainworker.h"

#define TURRIS_THERMO_POLL_INTERVAL 15
#define TURRIS_THERMO_SENSOR_NAME	"sa56004-i2c-0-4c"

CTurrisThermo::CTurrisThermo(const int ID)
{
	m_HwdID = ID;
	m_stoprequested = false;
	turrisThState = TURRIS_TH_INIT;
	nptr = NULL;
}

CTurrisThermo::~CTurrisThermo(void)
{
}

bool CTurrisThermo::StartHardware()
{
	//Start worker thread
	m_thread = boost::shared_ptr<boost::thread>(new boost::thread(boost::bind(&CTurrisThermo::Do_Work, this)));
	m_bIsStarted = true;
	sOnConnected(this);

	return (m_thread != NULL);
}

bool CTurrisThermo::StopHardware()
{
	if (m_thread != NULL)
	{
		assert(m_thread);
		m_stoprequested = true;
		m_thread->join();
	}
	m_bIsStarted = false;
    return true;
}

void CTurrisThermo::Do_Work()
{
	int nr = 0;
	sensors_chip_name name;
	int sec_counter = TURRIS_THERMO_POLL_INTERVAL - 5;

	while (!m_stoprequested)
	{
		switch(turrisThState) {
			case TURRIS_TH_INIT: {
				sensors_init(NULL);
				sensors_parse_chip_name(TURRIS_THERMO_SENSOR_NAME, &name);

				nptr = sensors_get_detected_chips(&name, &nr);
				if(nptr == NULL) {
					_log.Log(LOG_ERROR, "TurrisThermo: Temperature monitoring chip not found!");
					turrisThState = TURRIS_TH_END;
				}
				else {
					_log.Log(LOG_STATUS, "TurrisThermo: Worker started...");
					turrisThState = TURRIS_TH_READING;
				}
				break;
			}
			case TURRIS_TH_READING: {
				sleep_seconds(1);
				sec_counter++;
				if((sec_counter % 12) == 0)	{
					m_LastHeartbeat = mytime(NULL);
				}

				if ((sec_counter % TURRIS_THERMO_POLL_INTERVAL) == 0) {
					GetSensorDetails();
				}
				break;
			}
			case TURRIS_TH_END: {
				m_stoprequested = true;
				break;
			}
		}

	}

	sensors_free_chip_name(&name);
	sensors_cleanup();
	_log.Log(LOG_STATUS, "TurrisThermo: Worker stopped...");
}

bool CTurrisThermo::WriteToHardware(const char *pdata, const unsigned char length)
{
	return false;
}

void CTurrisThermo::GetSensorDetails()
{
	double val;
	int nf = 0;
	const sensors_feature* fptr;
	const sensors_subfeature* sptr;

	while((fptr = sensors_get_features(nptr, &nf)) != NULL) {
		sptr = sensors_get_subfeature(nptr, fptr, SENSORS_SUBFEATURE_TEMP_INPUT);
		sensors_get_value(nptr, sptr->number, &val);

		if(fptr->number == 0) {
			SendTempSensor(0, 100, (const float)val, "Turris Board Temp");
		}
		else if(fptr->number == 1) {
			SendTempSensor(1, 100, (const float)val, "Turris CPU Temp");
		}
	}

}
