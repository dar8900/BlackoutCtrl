//Programma per il controllo dei blackout

#include "Sim800l.h"
#include <SoftwareSerial.h>
#include <Chrono.h>
#include <LowPower.h>

#define NOP_PIN	 										   4
#define RED_LED											   5
#define GREEN_LED  									   6
#define SIM800_SLEEP_PIN						   7

#define MAX_TELEPHONE_NUMBER	       1
#define N_SAMPLE										100

#define SIM_SLEEP_ON	      						LOW
#define SIM_SLEEP_OFF      						HIGH


 //  RX  10       >>>   TX    
 //  TX   11       >>>   RX ci vuole un partitore di tensione da 5v a 3.3v

typedef struct
{
	int hour;
	int minute;
	int second;
	int day;
	int month;
	int year;
}TIME_DATE;

Sim800l Sim800l;  
TIME_DATE Time;
Chrono SendAlarmMessageTimer(Chrono::SECONDS);
Chrono NoOpLedTimer;
int TimerSms = 2;
bool StopSms;
bool NoOperating;


const char *TelephoneNumber [MAX_TELEPHONE_NUMBER] = 
{
	"+393336498423",
};

String SMSText = "BLACKOUT AVVENUTO";



void BlinkLed(int WichLed, int Delay, int AddedHighDelay = 0, int AddedLowDelay = 0)
{
	digitalWrite(WichLed, HIGH);
	delay(Delay + AddedHighDelay);
	digitalWrite(WichLed, LOW);
	delay(Delay + AddedLowDelay);
}


String GetTime()
{
	Sim800l.RTCtime(&Time.day, &Time.month, &Time.year, &Time.hour, &Time.minute, &Time.second);
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
		Sent = Sim800l.sendSms(TelephoneNumber[i], Text.c_str());
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
	SmsTxt = Sim800l.readSms(1);   
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

void setup()
{
	digitalWrite(SIM800_SLEEP_PIN, SIM_SLEEP_OFF);
	Serial.begin(9600);
	Sim800l.begin(); 
	Sim800l.updateRtc(2);  // UTC Roma
	pinMode(NOP_PIN, INPUT);
	pinMode(RED_LED, OUTPUT);
	pinMode(GREEN_LED, OUTPUT);
	pinMode(SIM800_SLEEP_PIN, OUTPUT);
	
	digitalWrite(GREEN_LED, LOW);
	digitalWrite(RED_LED, LOW);
	digitalWrite(SIM800_SLEEP_PIN, SIM_SLEEP_OFF);
}

void loop()
{
	if(digitalRead(NOP_PIN) == HIGH)
	{
		NoOperating = !NoOperating;
	}
	if(NoOperating)
	{
		
		SendAlarmMessageTimer.restart();
		if(NoOpLedTimer.hasPassed(2000, true))
		{
			for(int i  = 0; i < 5; i++)
				BlinkLed(GREEN_LED, 250);
		}
		else
			digitalWrite(GREEN_LED, LOW);
		digitalWrite(RED_LED, LOW);
	}
	else
	{
		NoOpLedTimer.restart();
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
			digitalWrite(GREEN_LED, LOW);
			BlinkLed(RED_LED, 250);
		}	
		else		
		{
			SendAlarmMessageTimer.restart();
			BlinkLed(GREEN_LED, 1000, 500, 0);
			digitalWrite(RED_LED, LOW);
		}
	}

}