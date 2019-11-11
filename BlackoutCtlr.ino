//Programma per il controllo dei blackout

#include <Sim800L.h>
#include <SoftwareSerial.h>
#include <Chrono.h>

#define NOP_PIN	 										   3
#define RED_LED											   5
#define GREEN_LED  										   6
#define SIM800_SLEEP_PIN								   7

#define BUTTON_PRESS_PIN								   8

#define RELAY_SWICH										   9

#define MAX_TELEPHONE_NUMBER	     					   1
#define N_SAMPLE										 100

#define PWM_PERCENT(Perc)								((int)(Perc * 255 / 100))

#define TIMEOUT_DELAY(Sec, Cnt)								(Sec * 1000 / Cnt)

#define POWER_DELAY(Hour)					     		(3600 * Hour)


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
	int hour;
	int minute;
	int second;
	int day;
	int month;
	int year;
}TIME_DATE;

Sim800L Gsm;  
TIME_DATE Time;
Chrono SendAlarmMessageTimer(Chrono::SECONDS);
Chrono SwitchPower(Chrono::SECONDS);
int TimerSms = 2;
bool StopSms;
bool NoSignal;
uint8_t PowerMode = MAIN_POWER;


const char *TelephoneNumber [MAX_TELEPHONE_NUMBER] = 
{
	"+393336498423",
};

String SMSText = "BLACKOUT AVVENUTO";



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
	Gsm.RTCtime(&Time.day, &Time.month, &Time.year, &Time.hour, &Time.minute, &Time.second);
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
	String BOText = SMSText + GetTime();
	SMSSent = SendSms(BOText);
	return SMSSent;
}

bool StopSmsSend()
{
	if(ReadSms() != "ok")
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


void setup()
{
	Serial.begin(9600);
	Gsm.begin(9600); 
	Gsm.updateRtc(2);  // UTC Roma
	pinMode(RED_LED, OUTPUT);
	pinMode(GREEN_LED, OUTPUT);
	pinMode(RELAY_SWICH, OUTPUT);
	digitalWrite(RELAY_SWICH, HIGH);
	analogWrite(GREEN_LED, PWM_PERCENT(0));
	analogWrite(RED_LED, PWM_PERCENT(0));
	WaitForNetwork();
	SwitchPower.start();
}

void loop()
{
	int Voltage = (int)(roundf(GetVoltage()));
	if(Voltage == 0)	
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
		BlinkFadedLed(RED_LED, PWM_PERCENT(100), 250);
	}	
	else		
	{
		SendAlarmMessageTimer.restart();
		BlinkFadedLed(GREEN_LED, PWM_PERCENT(100),1000, 500, 0);
		analogWrite(RED_LED, PWM_PERCENT(0));
	}

	// Se sono passate 2h distacchiamo la carica e andiamo a batteria per 1h
	SwitchPowerMode();
}
