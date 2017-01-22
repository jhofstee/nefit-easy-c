#include <stdlib.h>
#include <stdio.h>

#include "nefit-easy.h"

/* see https://github.com/robertklep/nefit-easy-core/wiki/List-of-endpoints */
const char *paths[] = {
	"/system/location/latitude",
	"/system/location/longitude",
	"/dhwCircuits/dhwA/dhwCurrentSwitchpoint",
	"/dhwCircuits/dhwA/dhwNextSwitchpoint",
	"/dhwCircuits/dhwA/dhwOffDuringAbsence",
	"/dhwCircuits/dhwA/dhwOffDuringNight",
	"/dhwCircuits/dhwA/dhwOperationMode",
	"/dhwCircuits/dhwA/dhwOperationType",
	"/dhwCircuits/dhwA/dhwProgram0",
	"/dhwCircuits/dhwA/dhwProgram1",
	"/dhwCircuits/dhwA/dhwProgram2",
	"/dhwCircuits/dhwA/extraDhw/duration",
	"/dhwCircuits/dhwA/extraDhw/status",
	"/dhwCircuits/dhwA/extraDhw/supported",
	"/dhwCircuits/dhwA/hotWaterSystem",
	"/dhwCircuits/dhwA/thermaldesinfect/lastresult",
	"/dhwCircuits/dhwA/thermaldesinfect/state",
	"/dhwCircuits/dhwA/thermaldesinfect/time",
	"/dhwCircuits/dhwA/thermaldesinfect/weekday",
	"/ecus/rrc/dayassunday/day",
	"/ecus/rrc/homeentrancedetection/userprofile",
	"/ecus/rrc/installerdetails",
	"/ecus/rrc/lockuserinterface",
	"/ecus/rrc/personaldetails",
	"/ecus/rrc/pirSensitivity",
	"/ecus/rrc/pm/closingvalve/status",
	"/ecus/rrc/pm/ignition/status",
	"/ecus/rrc/pm/refillneeded/status",
	"/ecus/rrc/pm/shorttapping/status",
	"/ecus/rrc/pm/systemleaking/status",
	"/ecus/rrc/recordings/yearTotal",
	"/ecus/rrc/selflearning/learnedprogram",
	"/ecus/rrc/selflearning/nextSwitchpoint",
	"/ecus/rrc/selflearning/nextSwitchpointEndtime",
	"/ecus/rrc/temperaturestep",
	"/ecus/rrc/uiStatus",
	"/ecus/rrc/userprogram/activeprogram",
	"/ecus/rrc/userprogram/fireplacefunction",
	"/ecus/rrc/userprogram/preheating",
	"/ecus/rrc/userprogram/program0",
	"/ecus/rrc/userprogram/program1",
	"/ecus/rrc/userprogram/program2",
	"/ecus/rrc/userprogram/userswitchpointname1",
	"/ecus/rrc/userprogram/userswitchpointname2",
	"/ecus/rrc/weatherDependent/basePointSupply",
	"/ecus/rrc/weatherDependent/endPointSupply",
	"/ecus/rrc/weatherDependent/forcedSwitchedOff",
	"/ecus/rrc/weatherDependent/maxSupply",
	"/ecus/rrc/weatherDependent/minSupply",
	"/ecus/rrc/weatherDependent/nightSwitchOff",
	"/ecus/rrc/weatherDependent/roomInfluence",
	"/ecus/rrc/weatherDependent/summerSwitchOff",
	"/gateway/brandID",
	"/gateway/remote/installername",
	"/gateway/remote/servicestate",
	"/gateway/remote/sid",
	"/gateway/remote/sidexptime",
	"/gateway/remote/tempsid",
	"/gateway/remote/tempsidexptime",
	"/gateway/serialnumber",
	"/gateway/update/strategy",
	"/gateway/uuid",
	"/gateway/versionFirmware",
	"/gateway/versionHardware",
	"/heatingCircuits/hc1/actualSupplyTemperature",
	"/heatingCircuits/hc1/control",
	"/heatingCircuits/hc1/holidayMode/activated",
	"/heatingCircuits/hc1/holidayMode/end",
	"/heatingCircuits/hc1/holidayMode/start",
	"/heatingCircuits/hc1/holidayMode/status",
	"/heatingCircuits/hc1/holidayMode/temperature",
	"/heatingCircuits/hc1/operationMode",
	"/heatingCircuits/hc1/temperatureAdjustment",
	"/heatingCircuits/hc1/manualTempOverride/status",
	"/heatingCircuits/hc1/type",
	"/heatingCircuits/hc1/usermode",
	"/notifications",
	"/system/appliance/boilermaintenancerequest",
	"/system/appliance/causecode",
	"/system/appliance/cm/type",
	"/system/appliance/cm/version",
	"/system/appliance/displaycode",
	"/system/appliance/serialnumber",
	"/system/appliance/systemPressure",
	"/system/appliance/type",
	"/system/appliance/version",
	"/system/interfaces/ems/brandbit",
	NULL
};

static void value_obtained(struct nefit_easy *easy, json_object *obj)
{
	EASY_UNUSED(easy);

	printf("%s\n", json_object_to_json_string(obj));
	fflush(stdout);
}

/* Request all values from the array above */
void get_values(struct nefit_easy *easy)
{
	char const **path;

	path = paths;
	while (*path != NULL)
		easy_get(easy, *path++);
}

/* Force a certain temperature */
void manual_temperature(struct nefit_easy *easy, double temperature)
{
	easy_put_double(easy, "/heatingCircuits/hc1/temperatureRoomManual", temperature);
	easy_put_string(easy, "/heatingCircuits/hc1/usermode", "manual");
}

/* Stop forcing a temperature and resume normal program */
void clock_mode(struct nefit_easy *easy)
{
	easy_put_string(easy, "/heatingCircuits/hc1/usermode", "clock");
}

int main(int argc, char **argv)
{
	struct nefit_easy easy;
	char *serial_number, *access_key, *password;
	EASY_UNUSED(argc);
	EASY_UNUSED(argv);

	/* init library */
	xmpp_initialize();

	serial_number = getenv("NEFIT_SERIAL_NUMBER");
	access_key = getenv("NEFIT_ACCESS_KEY");
	password = getenv("NEFIT_PASSWORD");

	if (!serial_number || !access_key || !password) {
		printf("The following environmental variabled must be set\n");
		printf("NEFIT_SERIAL_NUMBER=123456789\n");
		printf("NEFIT_ACCESS_KEY=abcdefhijklmnopq\n");
		printf("NEFIT_PASSWORD=wachtw\n");
		exit(1);
	}

	easy_connect(&easy, serial_number, access_key, password, value_obtained);

	get_values(&easy);
	//manual_temperature(&easy, 23);
	//clock_mode(&easy);

	/* enter the event loop */
	xmpp_run(easy.xmpp_ctx);

	/* release our connection and context */
	xmpp_conn_release(easy.xmpp_conn);
	xmpp_ctx_free(easy.xmpp_ctx);

	/* shutdown lib */
	xmpp_shutdown();

	return 0;
}
