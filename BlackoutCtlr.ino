//Programma per il controllo dei blackout

// Link per libreria https://github.com/VittorioEsposito/Sim800L-Arduino-Library-revised.git

#include <Sim800L.h>
#include <SoftwareSerial.h>
#include <Chrono.h>
#include <EEPROM.h>

#define NOP_PIN	 										   3
#define RED_LED											   5
#define GREEN_LED  										   6
#define RESET_EEPROM									   7

#define BUTTON_PRESS_PIN								   8

#define RELAY_SWICH										   9

#define MAX_TELEPHONE_NUMBER	     					   1
#define N_SAMPLE										 100

#define PWM_PERCENT(Perc)		   ((int)(Perc * 255 / 100))

#define TIMEOUT_DELAY(Sec, Cnt)			  (Sec * 1000 / Cnt)

#define POWER_DELAY(Hour)					   (3600 * Hour)

#define LAST_ADDRESS_ADDR								   0
#define ALARM_INIT_ADDR									   2


 //  RX  10       >>>   TX    
 //  TX  11       >>>   RX ci vuole un partitore di tensione da 5v a 3.3v

enum
{
	MINIMUM_FUNC = 0,
	NORMAL_FUNC = 1
};

enum
{
	MAIN_POWER = 0,
	BATTERY
};

typedef struct
{
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	uint8_t day;
	uint8_t month;
	uint8_t year;
}TIME_DATE;

typedef struct 
{
	TIME_DATE    AlarmTime;
	uint8_t      PowerMode;
}ALARM_DATA;

Sim800L Gsm;  
TIME_DATE Time;
Chrono SendAlarmMessageTimer(Chrono::SECONDS);
Chrono SwitchPower(Chrono::SECONDS);
int TimerSms = 2;
bool StopSms;
bool NoSignal;
bool BlackoutAlarm = false;
uint8_t PowerMode = MAIN_POWER;
ALARM_DATA AlarmInfo;
uint16_t LastAlarmAddr = ALARM_INIT_ADDR;


const char *TelephoneNumber [MAX_TELEPHONE_NUMBER] = 
{
	"+393336498423",
};

const String SMSText = "ALLARME BLACKOUT!";



void BlinkFadedLed(int WichLed, int MaxPwmValue, int Delay, int AddedHighDelay = 0, int AddedLowDelay = 0)
{
	for(int i = 0; i <= MaxPwmValue; i++)
	{
		analogWrite(WichLed, i);
		delayMicroseconds(392);
	}
	delay(Delay + AddedHighDelay);
	for(int i = MaxPwmValue; i >= 0; i--)
	{
		analogWrite(WichLed, i);
		delayMicroseconds(392);	
	}
	delay(Delay + AddedLowDelay);
}

void BlinkLed(int WichLed)
{
	digitalWrite(WichLed, HIGH);
	delayMicroseconds(5000);
	digitalWrite(WichLed, LOW);
	delayMicroseconds(5000);	
}

String GetTime()
{
	int timedate[6];
	Gsm.RTCtime(&timedate[0], &timedate[1], &timedate[2], &timedate[3], &timedate[4], &timedate[5]);
	Time.day    = (uint8_t) timedate[0];
	Time.month  = (uint8_t) timedate[1];
	Time.year   = (uint8_t) (timedate[2] % 100);
	Time.hour   = (uint8_t) timedate[3];
	Time.minute = (uint8_t) timedate[4];
	Time.second = (uint8_t) timedate[5];
	String TimeStr = String(Time.hour) + ":" + String(Time.minute) + ":" + String(Time.second) + "  " + String(Time.day) + "/" + String(Time.month) + "/" + String(Time.year);
	return TimeStr;
}

float GetVoltage()
{
	int Sample = 0;
	float VoltageRead = 0;
	for(Sample = 0; Sample < N_SAMPLE; Sample++)
	{
		VoltageRead += (analogRead(A0) * 0.00488);
	}
	VoltageRead /= N_SAMPLE;
	return VoltageRead;
}

bool SendSms(String Text)
{
	bool Sent = false, AllSent = true;
	for(int i = 0; i < MAX_TELEPHONE_NUMBER; i++)
	{
		Sent = Gsm.sendSms(TelephoneNumber[i], Text.c_str());
		if(!Sent)
		{
			AllSent = false;
			Serial.print("Messaggio non inviato al numero :");
			Serial.println(TelephoneNumber[i]);
		}
		delay(2000);
	}
	return AllSent;
}

String ReadSms()
{
	String SmsTxt = "";
	SmsTxt = Gsm.readSms(1);   
	return SmsTxt;
}


bool SendSmsAlarm()
{
	bool SMSSent = false;
	String BOText = SMSText;// + GetTime();
	SMSSent = SendSms(BOText);
	return SMSSent;
}

bool StopSmsSend()
{
	if(ReadSms().indexOf("ok") == -1)
		return false;
	else
		return true;
}

void WaitForNetwork()
{
	int TestConnTimeOut = 200, PwmLedTestConn = 100;
	while(1)
	{
		if(Gsm.signalQuality().indexOf("99") == -1)
			break;
		TestConnTimeOut--;
		if(TestConnTimeOut == 0)
		{
			NoSignal = true;
			break;
		}
		analogWrite(GREEN_LED, PWM_PERCENT(PwmLedTestConn));
		analogWrite(RED_LED, PWM_PERCENT(PwmLedTestConn));
		if(PwmLedTestConn > 0)
			PwmLedTestConn--;
		else
			PwmLedTestConn = 100;
		delay(TIMEOUT_DELAY(10, 200));
	}
}


void SwitchPowerMode()
{
	if(!BlackoutAlarm)
	{
		if(PowerMode == MAIN_POWER)
		{
			if(!SwitchPower.isRunning())
				SwitchPower.restart();
			if(SwitchPower.hasPassed(POWER_DELAY(2), true))
			{
				digitalWrite(RELAY_SWICH, LOW);
				PowerMode = BATTERY;
				SwitchPower.stop();
			}
		}
		else
		{
			if(!SwitchPower.isRunning())
				SwitchPower.restart();
			if(SwitchPower.hasPassed(POWER_DELAY(1), true))
			{
				digitalWrite(RELAY_SWICH, HIGH);
				PowerMode = MAIN_POWER;
				SwitchPower.stop();
			}		
		}	
	}	
	else
	{
		if(PowerMode != BATTERY)
		{
			digitalWrite(RELAY_SWICH, LOW);
			PowerMode = BATTERY;
			SwitchPower.stop();	
		}	
	}
}

bool IsMainPowerOn()
{
	bool StillOn = true;
	int Voltage = (int)(roundf(GetVoltage()));
	if(Voltage <= 1)
	{
		if(!BlackoutAlarm)
		{
			GetTime();
			AlarmInfo.AlarmTime = Time;
			AlarmInfo.PowerMode = PowerMode;
			EEPROM.put(LastAlarmAddr, AlarmInfo);
			LastAlarmAddr += 7;
			if(LastAlarmAddr >= EEPROM.length())
				LastAlarmAddr = ALARM_INIT_ADDR;
			EEPROM.put(LAST_ADDRESS_ADDR, LastAlarmAddr);
		}
		BlackoutAlarm = true;
		StillOn = false;
	}
	else
		BlackoutAlarm = false;
	return StillOn;
}

void ResetEeprom(bool CheckButton)
{
	if(digitalRead(RESET_EEPROM) == HIGH || !CheckButton)
	{
		analogWrite(GREEN_LED, PWM_PERCENT(100));
		analogWrite(RED_LED, PWM_PERCENT(100));
		for(int i = 0; i < EEPROM.length(); i++)
			EEPROM.update(i, 0);
		LastAlarmAddr = ALARM_INIT_ADDR;
		EEPROM.put(LAST_ADDRESS_ADDR, LastAlarmAddr);
		delay(2000);
		analogWrite(GREEN_LED, PWM_PERCENT(0));
		analogWrite(RED_LED, PWM_PERCENT(0));
	}
}

void setup()
{
	Serial.begin(9600);
	Gsm.begin(9600); 
	Gsm.updateRtc(2);  // UTC Roma
	pinMode(RED_LED, OUTPUT);
	pinMode(GREEN_LED, OUTPUT);
	pinMode(RELAY_SWICH, OUTPUT);
	pinMode(RESET_EEPROM, INPUT);

	if(EEPROM.read(0) == 0xFF && EEPROM.read(1) == 0xFF)
		ResetEeprom(false);

	EEPROM.get(LAST_ADDRESS_ADDR, LastAlarmAddr);
	EEPROM.get(LastAlarmAddr, AlarmInfo);

	digitalWrite(RELAY_SWICH, HIGH);
	analogWrite(GREEN_LED, PWM_PERCENT(0));
	analogWrite(RED_LED, PWM_PERCENT(0));
	WaitForNetwork();
	SwitchPower.start();
}

void loop()
{
	if(IsMainPowerOn())	
	{
		SendAlarmMessageTimer.restart();
		if(PowerMode == BATTERY)
			BlinkFadedLed(GREEN_LED, PWM_PERCENT(50),1000, 500, 0);
		else
			BlinkFadedLed(GREEN_LED, PWM_PERCENT(100),1000, 500, 0);		
		analogWrite(RED_LED, PWM_PERCENT(0));
	}	
	else		
	{
		if(StopSmsSend() && !StopSms)
		{
			StopSms = true;
		}
		if(SendAlarmMessageTimer.hasPassed(TimerSms, true) && !StopSms)
		{
			if(SendSmsAlarm())
				TimerSms = 1800;
			else
				TimerSms = 2;
		}
		analogWrite(GREEN_LED, PWM_PERCENT(0));
		if(PowerMode == BATTERY)
			BlinkFadedLed(RED_LED, PWM_PERCENT(50), 250);
		else
			BlinkFadedLed(RED_LED, PWM_PERCENT(100), 250);		
	}

	// Se sono passate 2h distacchiamo la carica e andiamo a batteria per 1h
	SwitchPowerMode();
	ResetEeprom(true);

}
