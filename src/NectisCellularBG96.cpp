#include "NectisCellularConfig.h"
#include "NectisCellularBG96.h"

#include "Internal/Debug.h"
#include "Internal/StringBuilder.h"
#include "Internal/ArgumentParser.h"
#include "NectisCellularHardware.h"
#include <string.h>
#include <limits.h>

#define RET_OK(val)						(ReturnOk(val))
#define RET_ERR(val,err)			(ReturnError(__LINE__, val, err))

#define INTERVAL              (2500)
#define RECEIVE_TIMEOUT       (5000)
#define CONNECT_ID_NUM				(12)
#define POLLING_INTERVAL			(100)

#define HTTP_USER_AGENT				"QUECTEL_MODULE"

#define ENDPOINT_URL          "http://unified.soracom.io"		// ENDPOINT_URL は固定しているが、SORACOM以外にも送信する必要があれば、引数として受け取るように変更。

#define LINEAR_SCALE(val, inMin, inMax, outMin, outMax)	(((val) - (inMin)) / ((inMax) - (inMin)) * ((outMax) - (outMin)) + (outMin))

////////////////////////////////////////////////////////////////////////////////////////
// Helper functions

static bool SplitUrl(const char* url, const char** host, int* hostLength, const char** uri, int* uriLength)
{
	if (strncmp(url, "http://", 7) == 0) {
		*host = &url[7];
	}
	else if (strncmp(url, "https://", 8) == 0) {
		*host = &url[8];
	}
	else {
		return false;
	}

	const char* ptr;
	for (ptr = *host; *ptr != '\0'; ptr++) {
		if (*ptr == '/') break;
	}
	*hostLength = ptr - *host;
	*uri = ptr;
	*uriLength = strlen(ptr);

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////
// WioCellular

bool NectisCellularBG96::ReturnError(int lineNumber, bool value, NectisCellularBG96::ErrorCodeType errorCode)
{
	_LastErrorCode = errorCode;

	char str[100];
	sprintf(str, "%d", lineNumber);
	DEBUG_PRINT("ERROR! ");
	DEBUG_PRINTLN(str);

	return value;
}

int NectisCellularBG96::ReturnError(int lineNumber, int value, NectisCellularBG96::ErrorCodeType errorCode)
{
	_LastErrorCode = errorCode;

	char str[100];
	sprintf(str, "%d", lineNumber);
	DEBUG_PRINT("ERROR! ");
	DEBUG_PRINTLN(str);

	return value;
}

bool NectisCellularBG96::IsBusy() const
{
	return digitalRead(MODULE_STATUS_PIN) ? false : true;
}

bool NectisCellularBG96::IsRespond()
{
#ifndef ARDUINO_ARCH_NRF52
	auto writeTimeout = Serial1.getWriteTimeout();
	Serial1.setWriteTimeout(10);
#endif // ARDUINO_ARCH_NRF52

	Stopwatch sw;
	sw.Restart();
	while (!_AtSerial.WriteCommandAndReadResponse("AT", "^OK$", 500, NULL)) {
		if (sw.ElapsedMilliseconds() >= 2000)
		{
#ifndef ARDUINO_ARCH_NRF52
			Serial1.setWriteTimeout(writeTimeout);
#endif // ARDUINO_ARCH_NRF52
			return false;
		}
	}

#ifndef ARDUINO_ARCH_NRF52
	Serial1.setWriteTimeout(writeTimeout);
#endif // ARDUINO_ARCH_NRF52
	return true;
}

bool NectisCellularBG96::Reset()
{
	digitalWrite(MODULE_RESET_PIN, HIGH);
	delay(200);
	digitalWrite(MODULE_RESET_PIN, LOW);
	delay(300);

	return true;
}

bool NectisCellularBG96::TurnOn()
{
	delay(100);
	digitalWrite(MODULE_PWRKEY_PIN, HIGH);
	delay(200);
	digitalWrite(MODULE_PWRKEY_PIN, LOW);

	return true;
}

bool NectisCellularBG96::HttpSetUrl(const char* url)
{
	StringBuilder str;
	if (!str.WriteFormat("AT+QHTTPURL=%d", strlen(url))) return false;
	_AtSerial.WriteCommand(str.GetString());
	if (!_AtSerial.ReadResponse("^CONNECT$", 500, NULL)) return false;

	_AtSerial.WriteBinary((const byte*)url, strlen(url));
	if (!_AtSerial.ReadResponse("^OK$", 500, NULL)) return false;

	return true;
}

//bool NectisCellularBG96::ReadResponseCallback(const char* response)
//{
//	return false;
//}

NectisCellularBG96::NectisCellularBG96() : _SerialAPI(&Serial1), _AtSerial(&_SerialAPI, this), _AccessTechnology(ACCESS_TECHNOLOGY_NONE), _SelectNetworkMode(SELECT_NETWORK_MODE_NONE)
{
}

NectisCellularBG96::ErrorCodeType NectisCellularBG96::GetLastError() const
{
	return _LastErrorCode;
}

void NectisCellularBG96::Init()
{
	////////////////////
	// Module

	// Power Supply
	pinMode(MODULE_PWR_PIN, OUTPUT); digitalWrite(MODULE_PWR_PIN, LOW);
	// Turn On/Off
	pinMode(MODULE_PWRKEY_PIN, OUTPUT); digitalWrite(MODULE_PWRKEY_PIN, LOW);
	pinMode(MODULE_RESET_PIN, OUTPUT); digitalWrite(MODULE_RESET_PIN, LOW);
	// Status Indication
	pinMode(MODULE_STATUS_PIN, INPUT_PULLUP);
	// Main UART Interface
	pinMode(MODULE_DTR_PIN, OUTPUT); digitalWrite(MODULE_DTR_PIN, LOW);

#ifndef ARDUINO_ARCH_NRF52
	Serial1.setReadBufferSize(100);
	Serial1.setWriteTimeout(0xffffffff);	// HAL_MAX_DELAY
#endif // ARDUINO_ARCH_NRF52

	Serial1.begin(115200);

	////////////////////
	// Led

	pinMode(LED_BUILTIN, OUTPUT); digitalWrite(LED_BUILTIN, HIGH);
	pinMode(LED_CONN, OUTPUT); digitalWrite(LED_CONN, LOW);

	////////////////////
	// Grove

	pinMode(GROVE_VCCB_PIN, OUTPUT); digitalWrite(GROVE_VCCB_PIN, LOW);

	////////////////////
	// SD

	// pinMode(SD_POWR_PIN, OUTPUT); digitalWrite(SD_POWR_PIN, LOW);
}

void NectisCellularBG96::PowerSupplyCellular(bool on)
{
	digitalWrite(MODULE_PWR_PIN, on ? HIGH : LOW);
}

void NectisCellularBG96::PowerSupplyGrove(bool on)
{
	digitalWrite(GROVE_VCCB_PIN, on ? HIGH : LOW);
}

bool NectisCellularBG96::TurnOnOrReset()
{
	std::string response;
	ArgumentParser parser;

	if (IsRespond()) {
		DEBUG_PRINTLN("Reset()");
		if (!Reset()) return RET_ERR(false, E_UNKNOWN);
	}
	else {
		DEBUG_PRINTLN("TurnOn()");
		if (!TurnOn()) return RET_ERR(false, E_UNKNOWN);
	}

	Stopwatch sw;
	sw.Restart();
	while (!_AtSerial.WriteCommandAndReadResponse("AT", "^OK$", 500, NULL)) {
		DEBUG_PRINT(".");
		if (sw.ElapsedMilliseconds() >= 10000) return RET_ERR(false, E_UNKNOWN);
	}
	DEBUG_PRINTLN("");

	if (!_AtSerial.WriteCommandAndReadResponse("ATE0", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
	_AtSerial.SetEcho(false);
#ifndef ARDUINO_ARCH_NRF52
	if (!_AtSerial.WriteCommandAndReadResponse("AT+IFC=2,2", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
#endif // ARDUINO_ARCH_NRF52

#if defined ARDUINO_NECTIS
	switch (_AccessTechnology)
	{
	case ACCESS_TECHNOLOGY_NONE:
		break;
	case ACCESS_TECHNOLOGY_LTE_M1:
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QCFG=\"nwscanseq\",02,1", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QCFG=\"nwscanmode\",3,1", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QCFG=\"iotopmode\",0,1", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
		break;
	case ACCESS_TECHNOLOGY_LTE_NB1:
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QCFG=\"nwscanseq\",03,1", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QCFG=\"nwscanmode\",3,1", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QCFG=\"iotopmode\",1,1", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
		break;
	default:
		return RET_ERR(false, E_UNKNOWN);
	}
#endif // ARDUINO_NECTIS

#if defined ARDUINO_WIO_3G
	sw.Restart();
	bool cpinReady;
	while (true) {
		_AtSerial.WriteCommand("AT+CPIN?");
		cpinReady = false;
		while (true) {
			if (!_AtSerial.ReadResponse("^(OK|\\+CPIN: READY|\\+CME ERROR: .*)$", 500, &response)) return RET_ERR(false, E_UNKNOWN);
			if (response == "+CPIN: READY") {
				cpinReady = true;
				continue;
			}
			break;
		}
		if (response == "OK" && cpinReady) break;

		if (sw.ElapsedMilliseconds() >= 10000) return RET_ERR(false, E_UNKNOWN);
		delay(POLLING_INTERVAL);
	}
#elif defined ARDUINO_NECTIS
	sw.Restart();

	bool cpinReady;
	while (true) {
		_AtSerial.WriteCommand("AT+CPIN?");
		cpinReady = false;
		while (true) {
			if (!_AtSerial.ReadResponse("^(OK|\\+CPIN: READY|\\+CME ERROR: .*)$", 500, &response)) return RET_ERR(false, E_UNKNOWN);
			if (response == "+CPIN: READY") {
				cpinReady = true;
				continue;
			}
			break;
		}
		if (response == "OK" && cpinReady) break;

		if (sw.ElapsedMilliseconds() >= 10000) return RET_ERR(false, E_UNKNOWN);
		delay(POLLING_INTERVAL);
	}

	while (true) {
		int status;

		_AtSerial.WriteCommand("AT+CEREG?");
		if (!_AtSerial.ReadResponse("^\\+CEREG: (.*)$", 500, &response)) return RET_ERR(false, E_UNKNOWN);
		parser.Parse(response.c_str());
		if (parser.Size() < 2) return RET_ERR(false, E_UNKNOWN);
		//resultCode = atoi(parser[0]);
		status = atoi(parser[1]);
		if (!_AtSerial.ReadResponse("^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
		if (0 <= status && status <= 5 && status != 4) break;

		if (sw.ElapsedMilliseconds() >= 10000) return RET_ERR(false, E_UNKNOWN);
		delay(POLLING_INTERVAL);
	}
#endif

	return RET_OK(true);
}

bool NectisCellularBG96::TurnOff()
{
	if (!_AtSerial.WriteCommandAndReadResponse("AT+QPOWD", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
	if (!_AtSerial.ReadResponse("^POWERED DOWN$", 60000, NULL)) return RET_ERR(false, E_UNKNOWN);

	return RET_OK(true);
}

int NectisCellularBG96::GetIMEI(char* imei, int imeiSize)
{
	std::string response;
	std::string imeiStr;

	_AtSerial.WriteCommand("AT+GSN");
	while (true) {
		if (!_AtSerial.ReadResponse("^(OK|[0-9]+)$", 500, &response)) return RET_ERR(-1, E_UNKNOWN);
		if (response == "OK") break;
		imeiStr = response;
	}

	if ((int)imeiStr.size() + 1 > imeiSize) return RET_ERR(-1, E_UNKNOWN);
	strcpy(imei, imeiStr.c_str());

	return RET_OK((int)strlen(imei));
}

int NectisCellularBG96::GetIMSI(char* imsi, int imsiSize)
{
	std::string response;
	std::string imsiStr;

	_AtSerial.WriteCommand("AT+CIMI");
	while (true) {
		if (!_AtSerial.ReadResponse("^(OK|[0-9]+)$", 500, &response)) return RET_ERR(-1, E_UNKNOWN);
		if (response == "OK") break;
		imsiStr = response;
	}

	if ((int)imsiStr.size() + 1 > imsiSize) return RET_ERR(-1, E_UNKNOWN);
	strcpy(imsi, imsiStr.c_str());

	return RET_OK((int)strlen(imsi));
}

int NectisCellularBG96::GetICCID(char* iccid, int iccidSize)
{
	std::string response;

	_AtSerial.WriteCommand("AT+QCCID");
	if (!_AtSerial.ReadResponse("^\\+QCCID: (.*)$", 500, &response)) return RET_ERR(-1, E_UNKNOWN);
	if (!_AtSerial.ReadResponse("^OK$", 500, NULL)) return RET_ERR(-1, E_UNKNOWN);
	response.erase(response.size() - 1, 1);

	if ((int)response.size() + 1 > iccidSize) return RET_ERR(-1, E_UNKNOWN);
	strcpy(iccid, response.c_str());

	return RET_OK((int)strlen(iccid));
}

int NectisCellularBG96::GetPhoneNumber(char* number, int numberSize)
{
	std::string response;
	ArgumentParser parser;
	std::string numberStr;

	_AtSerial.WriteCommand("AT+CNUM");
	while (true) {
		if (!_AtSerial.ReadResponse("^(OK|\\+CNUM: .*)$", 500, &response)) return RET_ERR(-1, E_UNKNOWN);
		if (response == "OK") break;

		if (numberStr.size() >= 1) continue;

		parser.Parse(response.c_str());
		if (parser.Size() < 2) return RET_ERR(-1, E_UNKNOWN);
		numberStr = parser[1];
	}

	if ((int)numberStr.size() + 1 > numberSize) return RET_ERR(-1, E_UNKNOWN);
	strcpy(number, numberStr.c_str());

	return RET_OK((int)strlen(number));
}

int NectisCellularBG96::GetReceivedSignalStrength()
{
	std::string response;
	ArgumentParser parser;

	_AtSerial.WriteCommand("AT+CSQ");
	if (!_AtSerial.ReadResponse("^\\+CSQ: (.*)$", 500, &response)) return RET_ERR(INT_MIN, E_UNKNOWN);

	parser.Parse(response.c_str());
	if (parser.Size() != 2) return RET_ERR(INT_MIN, E_UNKNOWN);
	int rssi = atoi(parser[0]);

	if (!_AtSerial.ReadResponse("^OK$", 500, NULL)) return RET_ERR(INT_MIN, E_UNKNOWN);

	if (rssi == 0) return RET_OK(-113);
	else if (rssi == 1) return RET_OK(-111);
	else if (2 <= rssi && rssi <= 30) return RET_OK((int)LINEAR_SCALE((double)rssi, 2, 30, -109, -53));
	else if (rssi == 31) return RET_OK(-51);
	else if (rssi == 99) return RET_OK(-999);

	return RET_OK(-999);
}

bool NectisCellularBG96::GetTime(struct tm* tim)
{
	std::string response;

	_AtSerial.WriteCommand("AT+QLTS=1");
	if (!_AtSerial.ReadResponse("^\\+QLTS: (.*)$", 500, &response)) return RET_ERR(false, E_UNKNOWN);
	if (!_AtSerial.ReadResponse("^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);

#if defined ARDUINO_WIO_3G
	if (strlen(response.c_str()) != 24) return RET_ERR(false, E_UNKNOWN);
	const char* parameter = response.c_str();

	if (parameter[0] != '"') return RET_ERR(false, E_UNKNOWN);
	if (parameter[3] != '/') return RET_ERR(false, E_UNKNOWN);
	if (parameter[6] != '/') return RET_ERR(false, E_UNKNOWN);
	if (parameter[9] != ',') return RET_ERR(false, E_UNKNOWN);
	if (parameter[12] != ':') return RET_ERR(false, E_UNKNOWN);
	if (parameter[15] != ':') return RET_ERR(false, E_UNKNOWN);
	if (parameter[21] != ',') return RET_ERR(false, E_UNKNOWN);
	if (parameter[23] != '"') return RET_ERR(false, E_UNKNOWN);

	int yearOffset = atoi(&parameter[1]);
	tim->tm_year = (yearOffset >= 80 ? 1900 : 2000) + yearOffset - 1900;
	tim->tm_mon = atoi(&parameter[4]) - 1;
	tim->tm_mday = atoi(&parameter[7]);
	tim->tm_hour = atoi(&parameter[10]);
	tim->tm_min = atoi(&parameter[13]);
	tim->tm_sec = atoi(&parameter[16]);
	tim->tm_wday = 0;
	tim->tm_yday = 0;
	tim->tm_isdst = 0;
#elif defined ARDUINO_NECTIS
	if (strlen(response.c_str()) != 26) return RET_ERR(false, E_UNKNOWN);
	const char* parameter = response.c_str();

	if (parameter[0] != '"') return RET_ERR(false, E_UNKNOWN);
	if (parameter[5] != '/') return RET_ERR(false, E_UNKNOWN);
	if (parameter[8] != '/') return RET_ERR(false, E_UNKNOWN);
	if (parameter[11] != ',') return RET_ERR(false, E_UNKNOWN);
	if (parameter[14] != ':') return RET_ERR(false, E_UNKNOWN);
	if (parameter[17] != ':') return RET_ERR(false, E_UNKNOWN);
	if (parameter[23] != ',') return RET_ERR(false, E_UNKNOWN);
	if (parameter[25] != '"') return RET_ERR(false, E_UNKNOWN);

	tim->tm_year = atoi(&parameter[1]) - 1900;
	tim->tm_mon = atoi(&parameter[6]) - 1;
	tim->tm_mday = atoi(&parameter[9]);
	tim->tm_hour = atoi(&parameter[12]);
	tim->tm_min = atoi(&parameter[15]);
	tim->tm_sec = atoi(&parameter[18]);
	tim->tm_wday = 0;
	tim->tm_yday = 0;
	tim->tm_isdst = 0;
#endif

	// Update tm_wday and tm_yday
	mktime(tim);

	return RET_OK(true);
}

#if defined ARDUINO_NECTIS
void NectisCellularBG96::SetAccessTechnology(AccessTechnologyType technology)
{
	_AccessTechnology = technology;
}
#endif // ARDUINO_NECTIS

void NectisCellularBG96::SetSelectNetwork(SelectNetworkModeType mode, const char* plmn)
{
	_SelectNetworkMode = mode;
	_SelectNetworkPLMN = plmn;
}

bool NectisCellularBG96::WaitForCSRegistration(long timeout)
{
	std::string response;
	ArgumentParser parser;

	Stopwatch sw;
	sw.Restart();
	while (true) {
		int status;

		_AtSerial.WriteCommand("AT+CREG?");
		if (!_AtSerial.ReadResponse("^\\+CREG: (.*)$", 500, &response)) return RET_ERR(false, E_UNKNOWN);
		parser.Parse(response.c_str());
		if (parser.Size() < 2) return RET_ERR(false, E_UNKNOWN);
		//resultCode = atoi(parser[0]);
		status = atoi(parser[1]);
		if (!_AtSerial.ReadResponse("^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
		if (status == 0) return RET_ERR(false, E_UNKNOWN);
		if (status == 1 || status == 5) break;

		if (sw.ElapsedMilliseconds() >= (unsigned long)timeout) return RET_ERR(false, E_UNKNOWN);
		delay(POLLING_INTERVAL);
	}

	return RET_OK(true);
}

bool NectisCellularBG96::WaitForPSRegistration(long timeout)
{
	std::string response;
	ArgumentParser parser;

#if defined ARDUINO_WIO_3G
	Stopwatch sw;
	sw.Restart();
	while (true) {
		int status;

		_AtSerial.WriteCommand("AT+CGREG?");
		if (!_AtSerial.ReadResponse("^\\+CGREG: (.*)$", 500, &response)) return RET_ERR(false, E_UNKNOWN);
		parser.Parse(response.c_str());
		if (parser.Size() < 2) return RET_ERR(false, E_UNKNOWN);
		//resultCode = atoi(parser[0]);
		status = atoi(parser[1]);
		if (!_AtSerial.ReadResponse("^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
		if (status == 0) return RET_ERR(false, E_UNKNOWN);
		if (status == 1 || status == 5) break;

		if (sw.ElapsedMilliseconds() >= (unsigned long)timeout) return RET_ERR(false, E_UNKNOWN);
		delay(POLLING_INTERVAL);
	}
#elif defined ARDUINO_NECTIS
	Stopwatch sw;
	sw.Restart();
	while (true) {
		int status;

		_AtSerial.WriteCommand("AT+CEREG?");
		if (!_AtSerial.ReadResponse("^\\+CEREG: (.*)$", 500, &response)) return RET_ERR(false, E_UNKNOWN);
		parser.Parse(response.c_str());
		if (parser.Size() < 2) return RET_ERR(false, E_UNKNOWN);
		//resultCode = atoi(parser[0]);
		status = atoi(parser[1]);
		if (!_AtSerial.ReadResponse("^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
		if (status == 0) return RET_ERR(false, E_UNKNOWN);
		if (status == 1 || status == 5) break;

		if (sw.ElapsedMilliseconds() >= (unsigned long)timeout) return RET_ERR(false, E_UNKNOWN);
		delay(POLLING_INTERVAL);
	}
#endif

	return RET_OK(true);
}

bool NectisCellularBG96::Activate(const char* accessPointName, const char* userName, const char* password, long waitForRegistTimeout)
{
	std::string response;
	ArgumentParser parser;
	Stopwatch sw;

	if (!WaitForPSRegistration(0)) {
		StringBuilder str;
		if (!str.WriteFormat("AT+QICSGP=1,1,\"%s\",\"%s\",\"%s\",3", accessPointName, userName, password)) return RET_ERR(false, E_UNKNOWN);
		if (!_AtSerial.WriteCommandAndReadResponse(str.GetString(), "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);

		sw.Restart();

		switch (_SelectNetworkMode)
		{
		case SELECT_NETWORK_MODE_NONE:
			break;
		case SELECT_NETWORK_MODE_AUTOMATIC:
			if (!_AtSerial.WriteCommandAndReadResponse("AT+COPS=0", "^OK$", waitForRegistTimeout, NULL)) return RET_ERR(false, E_UNKNOWN);
			break;
		case SELECT_NETWORK_MODE_MANUAL_IMSI:
		{
			char imsi[15 + 1];
			if (GetIMSI(imsi, sizeof(imsi)) < 0) return RET_ERR(false, E_UNKNOWN);
			if (strlen(imsi) < 4) return RET_ERR(false, E_UNKNOWN);
			StringBuilder str;
			if (!str.WriteFormat("AT+COPS=1,2,\"%.5s\"", imsi)) return RET_ERR(false, E_UNKNOWN);
			if (!_AtSerial.WriteCommandAndReadResponse(str.GetString(), "^OK$", waitForRegistTimeout, NULL)) return RET_ERR(false, E_UNKNOWN);
			break;
		}
		case SELECT_NETWORK_MODE_MANUAL:
		{
			if (_SelectNetworkPLMN.size() <= 0) return RET_ERR(false, E_UNKNOWN);
			StringBuilder str;
			if (!str.WriteFormat("AT+COPS=1,2,\"%s\"", _SelectNetworkPLMN.c_str())) return RET_ERR(false, E_UNKNOWN);
			if (!_AtSerial.WriteCommandAndReadResponse(str.GetString(), "^OK$", waitForRegistTimeout, NULL)) return RET_ERR(false, E_UNKNOWN);
			break;
		}
		default:
			return RET_ERR(false, E_UNKNOWN);
		}

		if (!WaitForPSRegistration(waitForRegistTimeout)) return RET_ERR(false, E_UNKNOWN);

		// for debug.
#ifdef CELLULAR_DEBUG
		char dbg[100];
		sprintf(dbg, "Elapsed time is %lu[msec.].", sw.ElapsedMilliseconds());
		DEBUG_PRINTLN(dbg);

		_AtSerial.WriteCommandAndReadResponse("AT+CREG?", "^OK$", 500, NULL);
		_AtSerial.WriteCommandAndReadResponse("AT+CGREG?", "^OK$", 500, NULL);
#if defined ARDUINO_NECTIS
		_AtSerial.WriteCommandAndReadResponse("AT+CEREG?", "^OK$", 500, NULL);
#endif // ARDUINO_NECTIS
#endif // CELLULAR_DEBUG
	}

	sw.Restart();
	while (true) {
		_AtSerial.WriteCommand("AT+QIACT=1");
		if (!_AtSerial.ReadResponse("^(OK|ERROR)$", 150000, &response)) return RET_ERR(false, E_UNKNOWN);
		if (response == "OK") break;
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QIGETERROR", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
		if (sw.ElapsedMilliseconds() >= 150000) return RET_ERR(false, E_UNKNOWN);
		delay(POLLING_INTERVAL);
	}

	// for debug.
#ifdef CELLULAR_DEBUG
	if (!_AtSerial.WriteCommandAndReadResponse("AT+QIACT?", "^OK$", 150000, NULL)) return RET_ERR(false, E_UNKNOWN);
#endif // CELLULAR_DEBUG

	return RET_OK(true);
}

bool NectisCellularBG96::Deactivate()
{
	if (!_AtSerial.WriteCommandAndReadResponse("AT+QIDEACT=1", "^OK$", 40000, NULL)) return RET_ERR(false, E_UNKNOWN);
	if (!_AtSerial.WriteCommandAndReadResponse("AT+COPS=2", "^OK$", 120000, NULL)) return RET_ERR(false, E_UNKNOWN);

	return RET_OK(true);
}

bool NectisCellularBG96::GetDNSAddress(IPAddress* ip1, IPAddress* ip2)
{
	std::string response;
	std::string ipsStr;
	ArgumentParser parser;

	_AtSerial.WriteCommand("AT+QIDNSCFG=1");
	while (true) {
		if (!_AtSerial.ReadResponse("^(OK|\\+QIDNSCFG: 1,.*)$", 500, &response)) return RET_ERR(false, E_UNKNOWN);
		if (response == "OK") break;
		ipsStr = response;
	}

	parser.Parse(&ipsStr.c_str()[13]);

	if (parser.Size() >= 1) {
		if (!ip1->fromString(parser[0])) return RET_ERR(false, E_UNKNOWN);
	}
	else {
		*ip1 = INADDR_NONE;
	}
	if (parser.Size() >= 2) {
		if (!ip2->fromString(parser[1])) return RET_ERR(false, E_UNKNOWN);
	}
	else {
		*ip2 = INADDR_NONE;
	}

	return RET_OK(true);
}

bool NectisCellularBG96::SetDNSAddress(const IPAddress& ip1)
{
	return SetDNSAddress(ip1, INADDR_NONE);
}

bool NectisCellularBG96::SetDNSAddress(const IPAddress& ip1, const IPAddress& ip2)
{
	StringBuilder str;
	if (!str.WriteFormat("AT+QIDNSCFG=1,\"%u.%u.%u.%u\",\"%u.%u.%u.%u\"", ip1[0], ip1[1], ip1[2], ip1[3], ip2[0], ip2[1], ip2[2], ip2[3])) return RET_ERR(false, E_UNKNOWN);
	if (!_AtSerial.WriteCommandAndReadResponse(str.GetString(), "^OK$", 150000, NULL)) return RET_ERR(false, E_UNKNOWN);

	return RET_OK(true);
}

int NectisCellularBG96::SocketOpen(const char* host, int port, SocketType type)
{
	std::string response;
	ArgumentParser parser;

	if (host == NULL || host[0] == '\0') return RET_ERR(-1, E_UNKNOWN);
	if (port < 0 || 65535 < port) return RET_ERR(-1, E_UNKNOWN);

	const char* typeStr;
	switch (type) {
	case SOCKET_TCP:
		typeStr = "TCP";
		break;
	case SOCKET_UDP:
		typeStr = "UDP";
		break;
	default:
		return RET_ERR(-1, E_UNKNOWN);
	}

	bool connectIdUsed[CONNECT_ID_NUM];
	for (int i = 0; i < CONNECT_ID_NUM; i++) connectIdUsed[i] = false;

	_AtSerial.WriteCommand("AT+QISTATE?");
	do {
		if (!_AtSerial.ReadResponse("^(OK|\\+QISTATE: .*)$", 10000, &response)) return RET_ERR(-1, E_UNKNOWN);
		if (strncmp(response.c_str(), "+QISTATE: ", 10) == 0) {
			parser.Parse(&response.c_str()[10]);
			if (parser.Size() >= 1) {
				int connectId = atoi(parser[0]);
				if (connectId < 0 || CONNECT_ID_NUM <= connectId) return RET_ERR(-1, E_UNKNOWN);
				connectIdUsed[connectId] = true;
			}
		}
	} while (response != "OK");

	int connectId;
	for (connectId = 0; connectId < CONNECT_ID_NUM; connectId++) {
		if (!connectIdUsed[connectId]) break;
	}
	if (connectId >= CONNECT_ID_NUM) return RET_ERR(-1, E_UNKNOWN);

	StringBuilder str;
	if (!str.WriteFormat("AT+QIOPEN=1,%d,\"%s\",\"%s\",%d", connectId, typeStr, host, port)) return RET_ERR(-1, E_UNKNOWN);
	if (!_AtSerial.WriteCommandAndReadResponse(str.GetString(), "^OK$", 150000, NULL)) return RET_ERR(-1, E_UNKNOWN);
	str.Clear();
	if (!str.WriteFormat("^\\+QIOPEN: %d,0$", connectId)) return RET_ERR(-1, E_UNKNOWN);
	if (!_AtSerial.ReadResponse(str.GetString(), 150000, NULL)) return RET_ERR(-1, E_UNKNOWN);

	return RET_OK(connectId);
}

bool NectisCellularBG96::SocketSend(int connectId, const byte* data, int dataSize)
{
	if (connectId >= CONNECT_ID_NUM) return RET_ERR(false, E_UNKNOWN);
	if (dataSize > 1460) return RET_ERR(false, E_UNKNOWN);

	StringBuilder str;
	if (!str.WriteFormat("AT+QISEND=%d,%d", connectId, dataSize)) return RET_ERR(false, E_UNKNOWN);
	_AtSerial.WriteCommand(str.GetString());
	if (!_AtSerial.ReadResponse("^>", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
	_AtSerial.WriteBinary(data, dataSize);
	if (!_AtSerial.ReadResponse("^SEND OK$", 5000, NULL)) return RET_ERR(false, E_UNKNOWN);

	return RET_OK(true);
}

bool NectisCellularBG96::SocketSend(int connectId, const char* data)
{
	return SocketSend(connectId, (const byte*)data, strlen(data));
}

int NectisCellularBG96::SocketReceive(int connectId, byte* data, int dataSize)
{
	std::string response;

	if (connectId >= CONNECT_ID_NUM) return RET_ERR(-1, E_UNKNOWN);

	StringBuilder str;
	if (!str.WriteFormat("AT+QIRD=%d", connectId)) return RET_ERR(-1, E_UNKNOWN);
	_AtSerial.WriteCommand(str.GetString());
	if (!_AtSerial.ReadResponse("^\\+QIRD: (.*)$", 500, &response)) return RET_ERR(-1, E_UNKNOWN);
	int dataLength = atoi(response.c_str());
	if (dataLength >= 1) {
		if (dataLength > dataSize) return RET_ERR(-1, E_UNKNOWN);
		if (!_AtSerial.ReadBinary(data, dataLength, 500)) return RET_ERR(-1, E_UNKNOWN);
	}
	if (!_AtSerial.ReadResponse("^OK$", 500, NULL)) return RET_ERR(-1, E_UNKNOWN);

	return RET_OK(dataLength);
}

int NectisCellularBG96::SocketReceive(int connectId, char* data, int dataSize)
{
	int dataLength = SocketReceive(connectId, (byte*)data, dataSize - 1);
	if (dataLength >= 0) data[dataLength] = '\0';

	return dataLength;
}

int NectisCellularBG96::SocketReceive(int connectId, byte* data, int dataSize, long timeout)
{
	Stopwatch sw;
	sw.Restart();
	int dataLength;
	while ((dataLength = SocketReceive(connectId, data, dataSize)) == 0) {
		if (sw.ElapsedMilliseconds() >= (unsigned long)timeout) return 0;
		delay(POLLING_INTERVAL);
	}
	return dataLength;
}

int NectisCellularBG96::SocketReceive(int connectId, char* data, int dataSize, long timeout)
{
	Stopwatch sw;
	sw.Restart();
	int dataLength;
	while ((dataLength = SocketReceive(connectId, data, dataSize)) == 0) {
		if (sw.ElapsedMilliseconds() >= (unsigned long)timeout) return 0;
		delay(POLLING_INTERVAL);
	}
	return dataLength;
}

bool NectisCellularBG96::SocketClose(int connectId)
{
	if (connectId >= CONNECT_ID_NUM) return RET_ERR(false, E_UNKNOWN);

	StringBuilder str;
	if (!str.WriteFormat("AT+QICLOSE=%d", connectId)) return RET_ERR(false, E_UNKNOWN);
	if (!_AtSerial.WriteCommandAndReadResponse(str.GetString(), "^OK$", 10000, NULL)) return RET_ERR(false, E_UNKNOWN);

	return RET_OK(true);
}

int NectisCellularBG96::HttpGet(const char* url, char* data, int dataSize)
{
	NectisCellularHttpHeader header;
	header["Accept"] = "*/*";
	header["User-Agent"] = HTTP_USER_AGENT;
	header["Connection"] = "Keep-Alive";

	return HttpGet(url, data, dataSize, header);
}

int NectisCellularBG96::HttpGet(const char* url, char* data, int dataSize, const NectisCellularHttpHeader& header)
{
	std::string response;
	ArgumentParser parser;

	if (strncmp(url, "https:", 6) == 0) {
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QHTTPCFG=\"sslctxid\",1", "^OK$", 500, NULL)) return RET_ERR(-1, E_UNKNOWN);
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QSSLCFG=\"sslversion\",1,4", "^OK$", 500, NULL)) return RET_ERR(-1, E_UNKNOWN);
#if defined ARDUINO_WIO_3G
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QSSLCFG=\"ciphersuite\",1,\"0XFFFF\"", "^OK$", 500, NULL)) return RET_ERR(-1, E_UNKNOWN);
#elif defined ARDUINO_NECTIS
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QSSLCFG=\"ciphersuite\",1,0XFFFF", "^OK$", 500, NULL)) return RET_ERR(-1, E_UNKNOWN);
#endif
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QSSLCFG=\"seclevel\",1,0", "^OK$", 500, NULL)) return RET_ERR(-1, E_UNKNOWN);
	}

	if (!_AtSerial.WriteCommandAndReadResponse("AT+QHTTPCFG=\"requestheader\",1", "^OK$", 500, NULL)) return RET_ERR(-1, E_UNKNOWN);

	if (!HttpSetUrl(url)) return RET_ERR(-1, E_UNKNOWN);

	const char* host;
	int hostLength;
	const char* uri;
	int uriLength;
	if (!SplitUrl(url, &host, &hostLength, &uri, &uriLength)) return RET_ERR(false, E_UNKNOWN);

	StringBuilder headerSb;
	headerSb.Write("GET ");
	if (uriLength <= 0) {
		headerSb.Write("/");
	}
	else {
		headerSb.Write(uri, uriLength);
	}
	headerSb.Write(" HTTP/1.1\r\n");
	headerSb.Write("Host: ");
	headerSb.Write(host, hostLength);
	headerSb.Write("\r\n");
	for (auto it = header.begin(); it != header.end(); it++) {
		headerSb.Write(it->first.c_str());
		headerSb.Write(": ");
		headerSb.Write(it->second.c_str());
		headerSb.Write("\r\n");
	}
	headerSb.Write("\r\n");
	DEBUG_PRINTLN("=== header");
	DEBUG_PRINTLN(headerSb.GetString());
	DEBUG_PRINTLN("===");

	StringBuilder str;
	if (!str.WriteFormat("AT+QHTTPGET=60,%d", headerSb.Length())) return RET_ERR(false, E_UNKNOWN);
	_AtSerial.WriteCommand(str.GetString());
	if (!_AtSerial.ReadResponse("^CONNECT$", 60000, NULL)) return RET_ERR(false, E_UNKNOWN);
	const char* headerStr = headerSb.GetString();
	_AtSerial.WriteBinary((const byte*)headerStr, strlen(headerStr));
	if (!_AtSerial.ReadResponse("^OK$", 1000, NULL)) return RET_ERR(false, E_UNKNOWN);
	if (!_AtSerial.ReadResponse("^\\+QHTTPGET: (.*)$", 60000, &response)) return RET_ERR(-1, E_UNKNOWN);

	if (std::equal(response.begin(), response.begin() + 1, "200") != 0) {
		Serial.println("### ERROR");
		Serial.printf("response=%s\n", &response);
		return RET_ERR(-1, E_UNKNOWN);
	}

	parser.Parse(response.c_str());

	if (parser.Size() < 1) return RET_ERR(-1, E_UNKNOWN);
	if (strcmp(parser[0], "0") != 0) return RET_ERR(-1, E_UNKNOWN);
	int contentLength = parser.Size() >= 3 ? atoi(parser[2]) : -1;

	_AtSerial.WriteCommand("AT+QHTTPREAD");
	if (!_AtSerial.ReadResponse("^CONNECT$", 1000, NULL)) return RET_ERR(-1, E_UNKNOWN);
	if (contentLength >= 0) {
		if (contentLength + 1 > dataSize) return RET_ERR(-1, E_UNKNOWN);
		if (!_AtSerial.ReadBinary((byte*)data, contentLength, 60000)) return RET_ERR(-1, E_UNKNOWN);
		data[contentLength] = '\0';
		if (!_AtSerial.ReadResponse("^OK$", 1000, NULL)) return RET_ERR(-1, E_UNKNOWN);
	}	else {
		if (!_AtSerial.ReadResponseQHTTPREAD(data, dataSize, 60000)) return RET_ERR(-1, E_UNKNOWN);
		contentLength = strlen(data);
	}
	if (!_AtSerial.ReadResponse("^\\+QHTTPREAD: 0$", 1000, NULL)) return RET_ERR(-1, E_UNKNOWN);

	return RET_OK(contentLength);
}

bool NectisCellularBG96::HttpPost(const char* url, const char* data, int* responseCode)
{
	constexpr char HTTP_CONTENT_TYPE[] = "application/json";

  NectisCellularHttpHeader header;
	header["Accept"] = "*/*";
	header["User-Agent"] = HTTP_USER_AGENT;
	header["Connection"] = "Keep-Alive";
	header["Content-Type"] = HTTP_CONTENT_TYPE;

	return HttpPost(url, data, responseCode, header);
}

bool NectisCellularBG96::HttpPost(const char* url, const char* data, int* responseCode, const NectisCellularHttpHeader& header)
{
	std::string response;
	ArgumentParser parser;

	if (strncmp(url, "https:", 6) == 0) {
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QHTTPCFG=\"sslctxid\",1", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QSSLCFG=\"sslversion\",1,4", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
#if defined ARDUINO_WIO_3G
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QSSLCFG=\"ciphersuite\",1,\"0XFFFF\"", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
#elif defined ARDUINO_NECTIS
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QSSLCFG=\"ciphersuite\",1,0XFFFF", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
#endif
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QSSLCFG=\"seclevel\",1,0", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
	}

	if (!_AtSerial.WriteCommandAndReadResponse("AT+QHTTPCFG=\"requestheader\",1", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);

	if (!HttpSetUrl(url)) return RET_ERR(false, E_UNKNOWN);

	const char* host;
	int hostLength;
	const char* uri;
	int uriLength;
	if (!SplitUrl(url, &host, &hostLength, &uri, &uriLength)) return RET_ERR(false, E_UNKNOWN);

	StringBuilder headerSb;
	headerSb.Write("POST ");
	if (uriLength <= 0) {
		headerSb.Write("/");
	}
	else {
		headerSb.Write(uri, uriLength);
	}
	headerSb.Write(" HTTP/1.1\r\n");
	headerSb.Write("Host: ");
	headerSb.Write(host, hostLength);
	headerSb.Write("\r\n");
	if (!headerSb.WriteFormat("Content-Length: %d\r\n", strlen(data))) return RET_ERR(false, E_UNKNOWN);
	for (auto it = header.begin(); it != header.end(); it++) {
		headerSb.Write(it->first.c_str());
		headerSb.Write(": ");
		headerSb.Write(it->second.c_str());
		headerSb.Write("\r\n");
	}
	headerSb.Write("\r\n");
	DEBUG_PRINTLN("=== header");
	DEBUG_PRINTLN(headerSb.GetString());
	DEBUG_PRINTLN("===");

	StringBuilder str;
	if (!str.WriteFormat("AT+QHTTPPOST=%d", headerSb.Length() + strlen(data))) return RET_ERR(false, E_UNKNOWN);
	_AtSerial.WriteCommand(str.GetString());
	if (!_AtSerial.ReadResponse("^CONNECT$", 60000, NULL)) return RET_ERR(false, E_UNKNOWN);
	const char* headerStr = headerSb.GetString();
	_AtSerial.WriteBinary((const byte*)headerStr, strlen(headerStr));
	_AtSerial.WriteBinary((const byte*)data, strlen(data));
	if (!_AtSerial.ReadResponse("^OK$", 1000, NULL)) return RET_ERR(false, E_UNKNOWN);
	if (!_AtSerial.ReadResponse("^\\+QHTTPPOST: (.*)$", 60000, &response)) return RET_ERR(false, E_UNKNOWN);
	parser.Parse(response.c_str());
	if (parser.Size() < 1) return RET_ERR(false, E_UNKNOWN);
	if (strcmp(parser[0], "0") != 0) return RET_ERR(false, E_UNKNOWN);
	if (parser.Size() < 2) {
		*responseCode = -1;
	}
	else {
		*responseCode = atoi(parser[1]);
	}

	return RET_OK(true);
}

//! Send a USSD message.
/*!
  \param in    a pointer to an input string (ASCII characters) which will be sent to SORACOM Beam/Funnel/Harvest
				 after converted to GSM default 7 bit alphabets. allowed up to 182 characters.
  \param out   a pointer to an output buffer to receive response message.
  \param outSize specify allocated size of `out` in bytes.
*/
bool NectisCellularBG96::SendUSSD(const char* in, char* out, int outSize)
{
	if (in == NULL || out == NULL) {
		return RET_ERR(false, E_UNKNOWN);
	}
	if (strlen(in) > 182) {
		DEBUG_PRINTLN("the maximum size of a USSD message is 182 characters.");
		return RET_ERR(false, E_UNKNOWN);
	}

	StringBuilder str;
	if (!str.WriteFormat("AT+CUSD=1,\"%s\"", in)) {
		DEBUG_PRINTLN("error while sending 'AT+CUSD'");
		return RET_ERR(false, E_UNKNOWN);
	}
	_AtSerial.WriteCommand(str.GetString());

	std::string response;
	if (!_AtSerial.ReadResponse("^\\+CUSD: [0-9],\"(.*)\",[0-9]+$", 120000, &response)) {
		DEBUG_PRINTLN("error while reading response of 'AT+CUSD'");
		return RET_ERR(false, E_UNKNOWN);
	}

	if ((int)response.size() + 1 > outSize) return RET_ERR(false, E_UNKNOWN);
	strcpy(out, response.c_str());

	return RET_OK(true);
}

void NectisCellularBG96::SystemReset()
{
	NVIC_SystemReset();
}


/*
 * The functions that have customized for Nectis and should be called from outside the library.
 * For CAMI qibanca nectis series on nRF52840. 
 */

void NectisCellularBG96::SoftReset() {
	SystemReset();
}

void NectisCellularBG96::Begin() {
	// Initialize Uart between BL654 and BG96.
	Serial1.setPins(MODULE_UART_RX_PIN, MODULE_UART_TX_PIN, MODULE_RTS_PIN, MODULE_CTS_PIN);
	Serial1.begin(115200);

	delay(200);
}

void NectisCellularBG96::End() {
	Serial1.end();
}

void NectisCellularBG96::InitLteM() {
	constexpr char APN[] = "soracom.io";
	constexpr char USERNAME[] = "sora";
	constexpr char PASSWORD[] = "sora";

#ifdef ARDUINO_NECTIS
	SetAccessTechnology(ACCESS_TECHNOLOGY_LTE_M1);
	SetSelectNetwork(SELECT_NETWORK_MODE_MANUAL_IMSI);
#endif

	Serial.println("### Turn on or reset.");
	if (!TurnOnOrReset()) {
		Serial.println("### ERROR!; TurnOnOrReset ###");
		return;
	}

	delay(100);

	Serial.printf("### Connecting to \"%s\".\n", APN);
	if (!Activate(APN, USERNAME, PASSWORD)) {
			Serial.println("### ERROR!; Activate ###");
			return;
	}
}

void NectisCellularBG96::InitNbIoT() {
	constexpr char APN[] = "mtc.gen";
	constexpr char USERNAME[] = "mtc";
	constexpr char PASSWORD[] = "mtc";

#ifdef ARDUINO_NECTIS
	SetAccessTechnology(ACCESS_TECHNOLOGY_LTE_NB1);
	SetSelectNetwork(SELECT_NETWORK_MODE_MANUAL_IMSI);
#endif

	Serial.println("### Turn on or reset.");
	if (!TurnOnOrReset()) {
		Serial.println("### ERROR!; TurnOnOrReset ###");
		return;
	}

	delay(100);

	Serial.printf("### Connecting to \"%s\".\n", APN);
	if (!Activate(APN, USERNAME, PASSWORD)) {
		Serial.println("### ERROR!; Activate ###");
		return;
	}
}

int NectisCellularBG96::GetReceivedSignalStrengthIndicator() {
	int rssi = GetReceivedSignalStrength();
	int rssi_count = 0;
	while (rssi == - 999) {
		rssi = GetReceivedSignalStrength();
		if (rssi_count == 10) {
			SoftReset();
		}
		rssi_count++;
		delay(1000);
	}

	return rssi;
}

bool NectisCellularBG96::IsTimeGot(struct tm *tim, bool jst) {
	std::string response;

	// AT+QLTS=1 -> Acquire UTC
	// AT+QLTS=2 -> Acquire JST
	if (jst) {
		_AtSerial.WriteCommand("AT+QLTS=2");
	} else {
		_AtSerial.WriteCommand("AT+QLTS=1");
	}
	
	if (!_AtSerial.ReadResponse("^\\+QLTS: (.*)$", 500, &response)) return RET_ERR(false, E_UNKNOWN);
	if (!_AtSerial.ReadResponse("^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
	
	if (strlen(response.c_str()) != 26) return RET_ERR(false, E_UNKNOWN);
	
	const char* parameter = response.c_str();

	if (parameter[0] != '"') return RET_ERR(false, E_UNKNOWN);
	if (parameter[5] != '/') return RET_ERR(false, E_UNKNOWN);
	if (parameter[8] != '/') return RET_ERR(false, E_UNKNOWN);
	if (parameter[11] != ',') return RET_ERR(false, E_UNKNOWN);
	if (parameter[14] != ':') return RET_ERR(false, E_UNKNOWN);
	if (parameter[17] != ':') return RET_ERR(false, E_UNKNOWN);
	if (parameter[23] != ',') return RET_ERR(false, E_UNKNOWN);
	if (parameter[25] != '"') return RET_ERR(false, E_UNKNOWN);

	tim->tm_year = atoi(&parameter[1]) - 1900;
	tim->tm_mon = atoi(&parameter[6]) - 1;
	tim->tm_mday = atoi(&parameter[9]);
	tim->tm_hour = atoi(&parameter[12]);
	tim->tm_min = atoi(&parameter[15]);
	tim->tm_sec = atoi(&parameter[18]);
	tim->tm_wday = 0;
	tim->tm_yday = 0;
	tim->tm_isdst = 0;
	
	// Update tm_wday and tm_yday
	mktime(tim);
	
	return RET_OK(true);
}

void NectisCellularBG96::GetCurrentTime(struct tm *tim, bool jst) {
	// Get time in JST.
	while (!IsTimeGot(tim, jst)) {
		Serial.println("### ERROR! ###");
		delay(1000);
	}
}


bool NectisCellularBG96::HttpPost(const char* url, const char* data, const int dataSize, int* responseCode, const NectisCellularHttpHeader& header)
{
	std::string response;
	ArgumentParser parser;

	if (strncmp(url, "https:", 6) == 0) {
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QHTTPCFG=\"sslctxid\",1", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QSSLCFG=\"sslversion\",1,4", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
#if defined ARDUINO_WIO_3G
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QSSLCFG=\"ciphersuite\",1,\"0XFFFF\"", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
#elif defined ARDUINO_NECTIS
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QSSLCFG=\"ciphersuite\",1,0XFFFF", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
#endif
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QSSLCFG=\"seclevel\",1,0", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
	}

	if (!_AtSerial.WriteCommandAndReadResponse("AT+QHTTPCFG=\"requestheader\",1", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);

	if (!HttpSetUrl(url)) return RET_ERR(false, E_UNKNOWN);

	const char* host;
	int hostLength;
	const char* uri;
	int uriLength;
	if (!SplitUrl(url, &host, &hostLength, &uri, &uriLength)) return RET_ERR(false, E_UNKNOWN);

	StringBuilder headerSb;
	headerSb.Write("POST ");
	if (uriLength <= 0) {
		headerSb.Write("/");
	}
	else {
		headerSb.Write(uri, uriLength);
	}
	headerSb.Write(" HTTP/1.1\r\n");
	headerSb.Write("Host: ");
	headerSb.Write(host, hostLength);
	headerSb.Write("\r\n");
	if (!headerSb.WriteFormat("Content-Length: %d\r\n", dataSize)) return RET_ERR(false, E_UNKNOWN);
	for (auto it = header.begin(); it != header.end(); it++) {
		headerSb.Write(it->first.c_str());
		headerSb.Write(": ");
		headerSb.Write(it->second.c_str());
		headerSb.Write("\r\n");
	}
	headerSb.Write("\r\n");
	DEBUG_PRINTLN("=== header");
	DEBUG_PRINTLN(headerSb.GetString());
	DEBUG_PRINTLN("===");

	StringBuilder str;
	if (!str.WriteFormat("AT+QHTTPPOST=%d", headerSb.Length() + dataSize)) return RET_ERR(false, E_UNKNOWN);
	_AtSerial.WriteCommand(str.GetString());
	if (!_AtSerial.ReadResponse("^CONNECT$", 60000, NULL)) return RET_ERR(false, E_UNKNOWN);
	const char* headerStr = headerSb.GetString();
	_AtSerial.WriteBinary((const byte*)headerStr, strlen(headerStr));
	_AtSerial.WriteBinary((const byte*)data, dataSize);
	if (!_AtSerial.ReadResponse("^OK$", 1000, NULL)) return RET_ERR(false, E_UNKNOWN);
	if (!_AtSerial.ReadResponse("^\\+QHTTPPOST: (.*)$", 60000, &response)) return RET_ERR(false, E_UNKNOWN);
	parser.Parse(response.c_str());
	if (parser.Size() < 1) return RET_ERR(false, E_UNKNOWN);
	if (strcmp(parser[0], "0") != 0) return RET_ERR(false, E_UNKNOWN);
	if (parser.Size() < 2) {
		*responseCode = -1;
	}
	else {
		*responseCode = atoi(parser[1]);
	}

	return RET_OK(true);
}

bool NectisCellularBG96::HttpPost(const char *url, const byte *data, const int dataSize, int *responseCode, const NectisCellularHttpHeader &header) {
	std::string response;
	ArgumentParser parser;
    
	if (strncmp(url, "https:", 6) == 0) {
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QHTTPCFG=\"sslctxid\",1", "^OK$", 500, NULL))
			return RET_ERR(false, E_UNKNOWN);
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QSSLCFG=\"sslversion\",1,4", "^OK$", 500, NULL))
			return RET_ERR(false, E_UNKNOWN);
#if defined ARDUINO_WIO_3G
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QSSLCFG=\"ciphersuite\",1,\"0XFFFF\"", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
#elif defined ARDUINO_NECTIS
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QSSLCFG=\"ciphersuite\",1,0XFFFF", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
#endif
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QSSLCFG=\"seclevel\",1,0", "^OK$", 500, NULL))
			return RET_ERR(false, E_UNKNOWN);
	}
    
	if (!_AtSerial.WriteCommandAndReadResponse("AT+QHTTPCFG=\"requestheader\",1", "^OK$", 500, NULL))
		return RET_ERR(false, E_UNKNOWN);
	
	if (!HttpSetUrl(url))
		return RET_ERR(false, E_UNKNOWN);
    
	const char *host;
	int hostLength;
	const char *uri;
	int uriLength;
	if (!SplitUrl(url, &host, &hostLength, &uri, &uriLength))
		return RET_ERR(false, E_UNKNOWN);
    
	StringBuilder headerSb;
	headerSb.Write("POST ");
	if (uriLength <= 0) {
		headerSb.Write("/");
	} else {
		headerSb.Write(uri, uriLength);
	}
	headerSb.Write(" HTTP/1.1\r\n");
	headerSb.Write("Host: ");
	headerSb.Write(host, hostLength);
	headerSb.Write("\r\n");
	if (!headerSb.WriteFormat("Content-Length: %d\r\n", dataSize))
		return RET_ERR(false, E_UNKNOWN);
	for (auto it = header.begin(); it != header.end(); it++) {
		headerSb.Write(it->first.c_str());
		headerSb.Write(": ");
		headerSb.Write(it->second.c_str());
		headerSb.Write("\r\n");
	}
	headerSb.Write("\r\n");
	DEBUG_PRINTLN("=== header");
	DEBUG_PRINTLN(headerSb.GetString());
	DEBUG_PRINTLN("===");
    
	StringBuilder str;
	if (!str.WriteFormat("AT+QHTTPPOST=%d", headerSb.Length() + dataSize))
		return RET_ERR(false, E_UNKNOWN);
	_AtSerial.WriteCommand(str.GetString());
	if (!_AtSerial.ReadResponse("^CONNECT$", 60000, NULL))
		return RET_ERR(false, E_UNKNOWN);
	const char *headerStr = headerSb.GetString();
	_AtSerial.WriteBinary((const byte *) headerStr, strlen(headerStr));
	_AtSerial.WriteBinary((const byte *) data, dataSize);
	if (!_AtSerial.ReadResponse("^OK$", 1000, NULL))
		return RET_ERR(false, E_UNKNOWN);
	if (!_AtSerial.ReadResponse("^\\+QHTTPPOST: (.*)$", 60000, &response))
		return RET_ERR(false, E_UNKNOWN);
	parser.Parse(response.c_str());
	if (parser.Size() < 1)
		return RET_ERR(false, E_UNKNOWN);
	if (strcmp(parser[0], "0") != 0)
		return RET_ERR(false, E_UNKNOWN);
	if (parser.Size() < 2) {
		*responseCode = -1;
	} else {
		*responseCode = atoi(parser[1]);
	}
    
	return RET_OK(true);
}

bool NectisCellularBG96::HttpPost2(const char *url, const char *postData, int postDataSize, char *recvData, int recvDataSize, int *respCode) {
	NectisCellularHttpHeader header;
	header["Accept"] = "*/*";
	header["User-Agent"] = HTTP_USER_AGENT;
	header["Connection"] = "Keep-Alive";
	header["Content-Type"] = "application/json";
	
	return HttpPost2(url, postData, postDataSize, recvData, recvDataSize, respCode, header);
}

bool NectisCellularBG96::HttpPost2(const char *url, const char *postData, int postDataSize, char *recvData, int recvDataSize, int *respCode, const NectisCellularHttpHeader &header) {
	std::string response;
	ArgumentParser parser;

	if (strncmp(url, "https:", 6) == 0) {
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QHTTPCFG=\"sslctxid\",1", "^OK$", 500, NULL))
			return RET_ERR(false, E_UNKNOWN);
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QSSLCFG=\"sslversion\",1,4", "^OK$", 500, NULL))
			return RET_ERR(false, E_UNKNOWN);
#if defined ARDUINO_WIO_3G
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QSSLCFG=\"ciphersuite\",1,\"0XFFFF\"", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
#elif defined ARDUINO_NECTIS
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QSSLCFG=\"ciphersuite\",1,0XFFFF", "^OK$", 500, NULL)) return RET_ERR(false, E_UNKNOWN);
#endif
		if (!_AtSerial.WriteCommandAndReadResponse("AT+QSSLCFG=\"seclevel\",1,0", "^OK$", 500, NULL))
			return RET_ERR(false, E_UNKNOWN);
	}
    
	if (!_AtSerial.WriteCommandAndReadResponse("AT+QHTTPCFG=\"requestheader\",1", "^OK$", 500, NULL))
		return RET_ERR(false, E_UNKNOWN);
	
	if (!HttpSetUrl(url))
		return RET_ERR(false, E_UNKNOWN);
    
	const char *host;
	int hostLength;
	const char *uri;
	int uriLength;
	if (!SplitUrl(url, &host, &hostLength, &uri, &uriLength))
		return RET_ERR(false, E_UNKNOWN);
    
	StringBuilder headerSb;
	headerSb.Write("POST ");
	if (uriLength <= 0) {
		headerSb.Write("/");
	} else {
		headerSb.Write(uri, uriLength);
	}
	headerSb.Write(" HTTP/1.1\r\n");
	headerSb.Write("Host: ");
	headerSb.Write(host, hostLength);
	headerSb.Write("\r\n");
	if (!headerSb.WriteFormat("Content-Length: %d\r\n", postDataSize))
		return RET_ERR(false, E_UNKNOWN);
	for (auto it = header.begin(); it != header.end(); it++) {
		headerSb.Write(it->first.c_str());
		headerSb.Write(": ");
		headerSb.Write(it->second.c_str());
		headerSb.Write("\r\n");
	}
	headerSb.Write("\r\n");
	DEBUG_PRINTLN("=== header");
	DEBUG_PRINTLN(headerSb.GetString());
	DEBUG_PRINTLN("===");
    
	StringBuilder str;
	if (!str.WriteFormat("AT+QHTTPPOST=%d", headerSb.Length() + postDataSize))
		return RET_ERR(false, E_UNKNOWN);
	_AtSerial.WriteCommand(str.GetString());
	if (!_AtSerial.ReadResponse("^CONNECT$", 60000, NULL))
		return RET_ERR(false, E_UNKNOWN);
	const char *headerStr = headerSb.GetString();
	_AtSerial.WriteBinary((const byte *) headerStr, strlen(headerStr));
	_AtSerial.WriteBinary((const byte *) postData, postDataSize);
	if (!_AtSerial.ReadResponse("^OK$", 1000, NULL))
		return RET_ERR(false, E_UNKNOWN);
	if (!_AtSerial.ReadResponse("^\\+QHTTPPOST: (.*)$", 60000, &response))
		return RET_ERR(false, E_UNKNOWN);
	parser.Parse(response.c_str());
	if (parser.Size() < 1)
		return RET_ERR(false, E_UNKNOWN);
	if (strcmp(parser[0], "0") != 0)
		return RET_ERR(false, E_UNKNOWN);
	if (parser.Size() < 2) {
		*respCode = -1;
	} else {
		*respCode = atoi(parser[1]);
	}
    
	if(parser.Size() == 3) {
		int contentLength = atoi(parser[2]);

		if(contentLength <= 0)	{
			return RET_ERR(-1, E_UNKNOWN);
		}

		if((contentLength + 1) < recvDataSize) {
			_AtSerial.WriteCommand("AT+QHTTPREAD");
			if (!_AtSerial.ReadResponse("^CONNECT$", 1000, NULL)) {
				return RET_ERR(-1, E_UNKNOWN);
			}
			if (!_AtSerial.ReadBinary((byte *) recvData, contentLength, 60000)) {
				return RET_ERR(-1, E_UNKNOWN);
			}
			recvData[contentLength] = '\0';

			if (!_AtSerial.ReadResponse("^OK$", 1000, NULL)) {
				return RET_ERR(-1, E_UNKNOWN);
			}
		}
	}

	return RET_OK(true);
}


void NectisCellularBG96::PostDataViaHTTP(byte *post_data, int data_size) {
	Serial.println("### Post BINARY/HTTP.");
	
	constexpr char HTTP_CONTENT_TYPE[] = "application/octet-stream";

	NectisCellularHttpHeader header;
	header["Accept"] = "*/*";
	header["User-Agent"] = HTTP_USER_AGENT;
	header["Connection"] = "Keep-Alive";
	header["Content-Type"] = HTTP_CONTENT_TYPE;
    
	int status;
	if (!HttpPost(ENDPOINT_URL, post_data, (const int)data_size, &status, header)) {
		Serial.println("### ERROR! ###");
		goto err;
	}
	Serial.print("Status=");
	Serial.println(status);

err:
	Serial.println("### Wait.");
	delay(INTERVAL);
}

void NectisCellularBG96::PostDataViaHTTP(char *post_data, int data_size) {
	Serial.println("### Post JSON/HTTP.");
	
	constexpr char HTTP_CONTENT_TYPE[] = "application/json";

	NectisCellularHttpHeader header;
	header["Accept"] = "*/*";
	header["User-Agent"] = HTTP_USER_AGENT;
	header["Connection"] = "Keep-Alive";
	header["Content-Type"] = HTTP_CONTENT_TYPE;
	
	int status;
	if (!HttpPost(ENDPOINT_URL, post_data, (const int)data_size, &status, header)) {
		Serial.println("### ERROR! ###");
		goto err;
	}
	Serial.print("Status=");
	Serial.println(status);

err:
	Serial.println("### Wait.");
	delay(INTERVAL);
}

void NectisCellularBG96::PostDataViaUdp(byte *post_data, int data_size) {
	Serial.println("### Open Socket.");

	int connectId;
	connectId = SocketOpen("uni.soracom.io", 23080, NECTIS_UDP);
	if (connectId < 0) {
		Serial.println("### ERROR! ###");
		goto err;
	}
	
	Serial.println("### Send BINARY/UDP.");
	if (!SocketSend(connectId, post_data, data_size)) {
		Serial.println("### ERROR! ###");
		goto err_close;
	}
    
	Serial.println("### Receive.");
	int length;
	length = SocketReceive(connectId, post_data, data_size, RECEIVE_TIMEOUT);
	if (length < 0) {
		Serial.println("### ERROR! ###");
		Serial.println(length);
		goto err_close;
	}
	if (length == 0) {
		Serial.println("### RECEIVE TIMEOUT! ###");
		goto err_close;
	}
	Serial.print("Receive=");
	Serial.print((char *)post_data);
	Serial.println("");

err_close:
	Serial.println("### Close.\n");
	if (!SocketClose(connectId)) {
		Serial.println("### ERROR! ###");
		goto err;
	}

err:
	delay(INTERVAL);
}

void NectisCellularBG96::PostDataViaUdp(char *post_data, int data_size) {
	Serial.println("### Open Socket.");

	int connectId;
	connectId = SocketOpen("uni.soracom.io", 23080, NECTIS_UDP);
	if (connectId < 0) {
		Serial.println("### ERROR! ###");
		goto err;
	}
	
	Serial.println("### Send JSON/UDP.");
	if (!SocketSend(connectId, post_data)) {
		Serial.println("### ERROR! ###");
		goto err_close;
	}
    
	Serial.println("### Receive.");
	int length;
	length = SocketReceive(connectId, post_data, data_size, RECEIVE_TIMEOUT);
	if (length < 0) {
		Serial.println("### ERROR! ###");
		Serial.println(length);
		goto err_close;
	}
	if (length == 0) {
		Serial.println("### RECEIVE TIMEOUT! ###");
		goto err_close;
	}
	Serial.print("Receive=");
	Serial.print(post_data);
	Serial.println("");

err_close:
	Serial.println("### Close.\n");
	if (!SocketClose(connectId)) {
		Serial.println("### ERROR! ###");
		goto err;
	}

err:
	delay(INTERVAL);
}