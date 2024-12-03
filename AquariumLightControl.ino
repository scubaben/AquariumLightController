#include "RTClib.h"
RTC_DS3231 rtc;
DateTime now;
DateTime morning;
DateTime midday;
DateTime evening;
DateTime night;
int photoperiodTimes[4][3]{
  {9,0,0}, //morning start HH, MM, SS
  {12,0,0}, //midday start
  {17,0,0}, //evening start
  {20,0,0}, //night start
};
int outputSettings[4][6] = { //output settings (0-255) Royal Blue, White, Violet, Cyan, Moonlight, Fan
  {179,26,179,179,0,179},  //morning
  {191,89,191,191,179,179}, //midday
  {179,0,179,179,0,179}, //evening
  {0,0,0,0,2,0} //night
};
int currentOutput[6]; //current light output
unsigned long rampLength[4] = {3600000,3600000,3600000,3600000}; //length of ramp in milliseconds for morning, midday, evening, and night
int outputPins[6] = {3,5,6,9,10,11}; //PWM pins on an arduino metro mini
unsigned long interval[4][6];
unsigned long lastUpdate[6];
int priorPhotoperiod;
bool serialEnabled = true; //enables serial logging - only set this to true when connected to serial

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);  //builtin led used to alert about rtc errors
  for (int i = 0;  i < 6; i++){
    pinMode(outputPins[i], OUTPUT);  //initialize the pins
  }
  if(serialEnabled){
    Serial.begin(57600);
    while(!Serial); //wait for serial to connect
  }
    if(!rtc.begin()){
      if(serialEnabled){
        Serial.println("Couldn't find RTC");
        Serial.flush();
      }
      digitalWrite(LED_BUILTIN, HIGH);
      while(1);
    }
    if(rtc.lostPower()){
      if(serialEnabled){
        Serial.println("RTC lost power, setting time...");
      }
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); //this line sets the rtc time to the compilation time
      //rtc.adjust(DateTime(2020, 1, 1, 9, 0, 0)); //this line sets the rtc time to 9:00 AM, so its predictable if power is lost in the future
      digitalWrite(LED_BUILTIN, HIGH);
    }
    now = rtc.now();
    if(serialEnabled){
      Serial.println("Current date and time: ");
      Serial.print(now.year(), DEC);
      Serial.print('/');
      Serial.print(now.month(), DEC);
      Serial.print('/');
      Serial.print(now.day(), DEC);
      Serial.println(now.hour(), DEC);
      Serial.print(':');
      Serial.print(now.minute(), DEC);
      Serial.print(':');
      Serial.print(now.second(), DEC);
      for (int i = 0; i < 4; i++){
        switch (i) {
          case 0: Serial.println("Morning light settings");
          case 1: Serial.println("Midday light settings");
          case 2: Serial.println("Evening light settings");
          case 3: Serial.println("Night light settings");
        }
        for (int j = 0;  j < 6; j++){
          Serial.println("Channel ");
          Serial.print(i+1, DEC);
          Serial.print(':');
          Serial.print(outputSettings[i][j], DEC);
        }
      }
    }
  for(int i = 0; i < 6; i++){ //set the current output according to the photoperiod
    currentOutput[i] = outputSettings[determinePhotoperiod()][i];
  }
  for(int i = 0; i < 4; i++){ //calculate the ramp intervals
    for(int j = 0; j < 6; j++){
      interval[i][j] = millisPerStep(outputSettings[i+1][j], outputSettings[i][j], rampLength[i]);
    }
  }
}

void loop() {
  now = rtc.now();
  int photoperiod = determinePhotoperiod();
  if(!outputCorrect(photoperiod)){ //if the output doesn't match the photoperiod, ramp
    ramp(photoperiod);
  }
  if(photoperiod != priorPhotoperiod){
    if(serialEnabled){
      Serial.println("As of:");
      Serial.println(now.year(), DEC);
      Serial.print('/');
      Serial.print(now.month(), DEC);
      Serial.print('/');
      Serial.print(now.day(), DEC);
      Serial.println(now.hour(), DEC);
      Serial.print(':');
      Serial.print(now.minute(), DEC);
      Serial.print(':');
      Serial.print(now.second(), DEC);
      Serial.println("The photoperiod is now; ");
      switch (photoperiod){
        case 0: Serial.print("morning");
        case 1: Serial.print("midday");
        case 2: Serial.print("evening");
        case 3: Serial.print("night");
      }
    }
    priorPhotoperiod = photoperiod;
  }
}

int determinePhotoperiod(){
  if(now >= morning && now < midday){
    night = DateTime(now.year(), now.month(), now.day(), photoperiodTimes[3][0], photoperiodTimes[3][1], photoperiodTimes[3][2]); //once night is over, the next one will start later today (always assumes the day rollover happens during the night)
    return 0; //morning
  }
  else if(now >= midday && now < evening){
    morning = DateTime(now.year(), now.month(), now.day(), photoperiodTimes[0][0], photoperiodTimes[0][1], photoperiodTimes[0][2]) + TimeSpan(1,0,0,0); //now that morning is over, the next one will start tomorrow
    return 1; //midday
  }
  else if(now >= evening && now < night){
    midday = DateTime(now.year(), now.month(), now.day(), photoperiodTimes[1][0], photoperiodTimes[1][1], photoperiodTimes[1][2]) + TimeSpan(1,0,0,0); //now that midday is over, the next one will start tomorrow
    return 2; //evening
  }
  else {
    midday = DateTime(now.year(), now.month(), now.day(), photoperiodTimes[2][0], photoperiodTimes[2][1], photoperiodTimes[2][2]) + TimeSpan(1,0,0,0); //now that evening is over, the next one will start tomorrow
    return 3; //night
  }
}

bool outputCorrect(int photoperiod){
  for(int i = 0; i < 6; i++){
    if(currentOutput[i] != outputSettings[photoperiod][i]){
      return false;
    }
  }
  return true;
}

void ramp(int photoperiod){
  for(int i = 0; i < 6; i++){
    if(millis() > (lastUpdate[i] + interval[photoperiod][i])){ //if it is time to make and update
      if(currentOutput[i] > outputSettings[photoperiod][i]){ //if the current setting is greater than the setting for the current photoperiod then reduce by 1
        currentOutput[i]--;
        lastUpdate[i] = millis();
      }
      else if(currentOutput[i] < outputSettings[photoperiod][i]){ //if the current setting is less than the setting for the current photoperiod then increase by 1
        currentOutput[i]++;
        lastUpdate[i] = millis();
      }
      else{
        lastUpdate[i] = millis();
      }
      if(serialEnabled){
        Serial.println("Ramping channel ");
        Serial.print(i+1, DEC);
        Serial.print("to ");
        Serial.print(currentOutput[i], DEC);
      }
    }
  }
}

unsigned long millisPerStep(int ending, int beginning, unsigned long duration){
  unsigned long perStep = duration / (abs(ending - beginning));
  return perStep;
}