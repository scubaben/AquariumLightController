#include "RTClib.h"
RTC_DS3231 rtc;
DateTime now;
DateTime morning;
DateTime midday;
DateTime evening;
DateTime night;
DateTime lastMidnight;
int photoperiodTimes[4][3]{
  {9,0,0}, //morning start HH, MM, SS
  {12,0,0}, //midday start
  {16,30,0}, //evening start
  {19,30,0}, //night start
};
int outputSettings[4][6] = { //output settings (0-255) Royal Blue, White, Violet, Cyan, Moonlight, Fan (anything less than 50 for the fan is off)
  {179,26,179,179,10,179},  //morning
  {191,89,191,191,179,179}, //midday
  {179,26,179,179,10,179}, //evening
  {0,0,0,0,2,0} //night
};
int currentOutput[6]; //current light output
unsigned long rampLength[4] = {3600000,1800000,1800000,3600000}; //length of ramp in milliseconds for morning, midday, evening, and night
int outputPins[6] = {3,5,6,9,10,11}; //PWM pins on an arduino metro mini
unsigned long interval[4][6];
unsigned long lastUpdate[6];
int priorPhotoperiod;
bool serialEnabled = false; //enables serial logging - only set this to true when connected to serial
unsigned long lastTimePoll;
unsigned long timePollInterval = 1000;


void setup() {
  pinMode(LED_BUILTIN, OUTPUT);  //builtin led used to alert about rtc and serial errors
  for (int i = 0;  i < 6; i++){
    pinMode(outputPins[i], OUTPUT);  //initialize the pins
  }
  if(serialEnabled){
    Serial.begin(57600);
    digitalWrite(LED_BUILTIN, HIGH);
    while(!Serial); //wait for serial to connect
  }
  digitalWrite(LED_BUILTIN, LOW);
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
      //rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); //this line sets the rtc time to the compilation time
      rtc.adjust(DateTime(2020, 1, 1, 9, 0, 0)); //this line sets the rtc time to 9:00 AM, so its predictable if power is lost in the future
      digitalWrite(LED_BUILTIN, HIGH);
    }
    delay(50); //short delay before initializing times to try to avoid clock errors
    //initialize times
    now = rtc.now();
    lastTimePoll = millis();
    morning = DateTime(now.year(), now.month(), now.day(), photoperiodTimes[0][0], photoperiodTimes[0][1], photoperiodTimes[0][2]);
    midday = DateTime(now.year(), now.month(), now.day(), photoperiodTimes[1][0], photoperiodTimes[1][1], photoperiodTimes[1][2]);
    evening = DateTime(now.year(), now.month(), now.day(), photoperiodTimes[2][0], photoperiodTimes[2][1], photoperiodTimes[2][2]);
    night = DateTime(now.year(), now.month(), now.day(), photoperiodTimes[3][0], photoperiodTimes[3][1], photoperiodTimes[3][2]);
    lastMidnight = DateTime(now.year(), now.month(), now.day(), 0, 0, 0);
    if(serialEnabled){
      Serial.println("Starting up...");
      Serial.print("Current date and time: ");
      printDateTime(now);
      Serial.print("The current photoperiod is: ");
      printPhotoperiod(determinePhotoperiod());
      for (int i = 0; i < 4; i++){
        switch (i) {
          case 0: Serial.println("Morning light settings");
          break;
          case 1: Serial.println("Midday light settings");
          break;
          case 2: Serial.println("Evening light settings");
          break;
          case 3: Serial.println("Night light settings");
          break;
        }
        for (int j = 0;  j < 6; j++){
          Serial.print("Channel ");
          Serial.print(j+1, DEC);
          Serial.print(": ");
          Serial.println(outputSettings[i][j], DEC);
        }
      }
    }
  for(int i = 0; i < 6; i++){ //set the current output according to the photoperiod
    currentOutput[i] = outputSettings[determinePhotoperiod()][i];
  }
  for(int i = 0; i < 4; i++){ //calculate the ramp intervals for each photoperiod change
    for(int j = 0; j < 6; j++){
      if(i>0){
        interval[i][j] = millisPerStep(outputSettings[i][j], outputSettings[i-1][j], rampLength[i]); //use the prior photoperiod as the beginning state for calculating the ramp
      }
      else {
        interval[i][j] = millisPerStep(outputSettings[i][j], outputSettings[3][j], rampLength[i]); //for the morning photoperiod, use the night photperiod as the beginning
      }
    }
  }
  priorPhotoperiod = determinePhotoperiod();
}

void loop() {
  if(millis() > (lastTimePoll + timePollInterval)){
    now = rtc.now();
    lastTimePoll = millis();
  }
  while((now > (lastMidnight + TimeSpan(1,0,0,5))) || (now < lastMidnight)){ //if the current time is before or more than a day after the last midnight, there was probably an error so try to get the time again.
    if(serialEnabled){
      Serial.println("Bad date/time: ");
      printDateTime(now);
      Serial.println("Refreshing date/time...");
    }
    delay(50); //a short delay to let the processor catch up and to let transient errors resolve themselves before attempting to get the time again.
    now = rtc.now();
    if(serialEnabled){
      printDateTime(now);
    }
  }
  int photoperiod = determinePhotoperiod();
  if(photoperiod != priorPhotoperiod){ //detect a change in photoperiods
    if(serialEnabled){
      Serial.print("As of: ");
      printDateTime(now);
      Serial.print("The photoperiod is now: ");
      printPhotoperiod(photoperiod);
    }
    priorPhotoperiod = photoperiod;
  }
  if(!outputCorrect(photoperiod)){ //if the output doesn't match the photoperiod, ramp
    ramp(photoperiod);
  }
  updateOutput();
  if(now > lastMidnight + TimeSpan(1,0,0,0)){ //each midnight roll photoperiod times forward to the next day
    nextDay();
  }
}

int determinePhotoperiod(){
  if(now >= morning && now < midday){
    return 0; //morning
  }
  else if(now >= midday && now < evening){
    return 1; //midday
  }
  else if(now >= evening && now < night){
    return 2; //evening
  }
  else {
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
        if(serialEnabled){
          Serial.print("Ramping channel ");
          Serial.print(i+1, DEC);
          Serial.print(" down to ");
          Serial.print(currentOutput[i], DEC);
          Serial.print(" - ");
          printDateTime(now);
        }
      }
      else if(currentOutput[i] < outputSettings[photoperiod][i]){ //if the current setting is less than the setting for the current photoperiod then increase by 1
        currentOutput[i]++;
        lastUpdate[i] = millis();
        if(serialEnabled){
          Serial.print("Ramping channel ");
          Serial.print(i+1, DEC);
          Serial.print(" up to ");
          Serial.print(currentOutput[i], DEC);
          Serial.print(" - ");
          printDateTime(now);
        }
      }
      else{
        lastUpdate[i] = millis();
      }
    }
  }
}

unsigned long millisPerStep(int ending, int beginning, unsigned long duration){
  unsigned long perStep = duration / (abs((ending - beginning)));
  return perStep;
}

void printDateTime(DateTime timeToPrint){
  Serial.print(timeToPrint.year(), DEC);
  Serial.print('/');
  Serial.print(timeToPrint.month(), DEC);
  Serial.print('/');
  Serial.print(timeToPrint.day(), DEC);
  Serial.print(" - ");
  Serial.print(timeToPrint.hour(), DEC);
  Serial.print(':');
  Serial.print(timeToPrint.minute(), DEC);
  Serial.print(':');
  Serial.println(timeToPrint.second(), DEC);
}

void printPhotoperiod(int photoperiod){
  switch (photoperiod){
    case 0: Serial.println("Morning");
    break;
    case 1: Serial.println("Midday");
    break;
    case 2: Serial.println("Evening");
    break;
    case 3: Serial.println("Night");
    break;
  }
}

void nextDay(){
  morning = morning + TimeSpan(1,0,0,0);
  midday = midday + TimeSpan(1,0,0,0);
  evening = evening + TimeSpan(1,0,0,0);
  night = night + TimeSpan(1,0,0,0);
  lastMidnight = lastMidnight + TimeSpan(1,0,0,0);
  if(serialEnabled){
    printDateTime(now);
    Serial.println("It's midnight: rolling morning, midday, evening, and night times forward");
  }
}

void updateOutput(){
  for(int i = 0; i < 6; i++){
    analogWrite(outputPins[i], currentOutput[i]);
  }
}