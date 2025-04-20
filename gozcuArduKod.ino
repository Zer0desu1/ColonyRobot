#include <Servo.h>
#include <TinyGPS++.h>
#include <SoftwareSerial.h>
// Define servo objects
Servo panServo;  // Left-right movement
Servo tiltServo; // Up-down movement
Servo shooterServo;
// Define servo pins
const int PAN_SERVO_PIN = 9;
const int TILT_SERVO_PIN = 10;

// Define servo center/stop values (for continuous rotation servos)
// These values may need calibration for your specific servos
const int PAN_STOP = 90;  // Value to stop the pan servo
const int TILT_STOP = 90; // Value to stop the tilt servo

// Variables for smooth motion control
int panSpeed = 0;  // Current speed: -10 to +10
int tiltSpeed = 0; // Current speed: -10 to +10

// Maximum speed (deviation from stop value)
const int MAX_SPEED = 10;

// Rate of speed change for acceleration/deceleration
const int ACCEL_RATE = 2;

// Time tracking for smooth updates
unsigned long lastUpdateTime = 0;
const int UPDATE_INTERVAL = 50; // ms between updates
unsigned long lastUpdatedTimeShooter=0;

int RXPin = 2;
int TXPin = 3;
int GPSBaud = 9600;

TinyGPSPlus gps;
SoftwareSerial gpsSerial(RXPin, TXPin);

void setup() {
  gpsSerial.begin(GPSBaud);

  pinMode(7,OUTPUT); //Röle
  digitalWrite(7,HIGH);
  // Initialize servos
  panServo.attach(PAN_SERVO_PIN);
  tiltServo.attach(TILT_SERVO_PIN);
  shooterServo.attach(11);
  // Set servos to center position (stopped)
  panServo.write(PAN_STOP);
  tiltServo.write(TILT_STOP);
  
  // Start serial communication
  Serial.begin(115200);
  Serial.println("Pan-Tilt System Ready");
}

void loop() {
  // Process commands if available
  unsigned long currentTime = millis();
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    
    // Parse command format "P,T" where P is pan value (-100 to 100) and T is tilt value (-100 to 100)
    if (command.length() > 2 && command.indexOf(',') > 0) {
      digitalWrite(7,LOW);
      int firstComma = command.indexOf(',');
      int secondComma = command.indexOf(',', firstComma + 1);
      int panValue = command.substring(0, firstComma).toInt();
      int tiltValue = command.substring(firstComma + 1, secondComma).toInt();
      int precision = command.substring(secondComma + 1).toInt();  // new
      

      // Map received values (-100 to 100) to target speeds (-MAX_SPEED to MAX_SPEED)
      int targetPanSpeed = map(constrain(panValue, -100, 100), -100, 100, MAX_SPEED, -MAX_SPEED);
      int targetTiltSpeed = map(constrain(tiltValue, -100, 100), -100, 100, MAX_SPEED, -MAX_SPEED);

      if(currentTime - lastUpdatedTimeShooter >=3000 && abs(panValue)<20 && precision>=50){

        shooterServo.write(0);
        delay(50);
        shooterServo.write(75);

        lastUpdatedTimeShooter=currentTime;
      }

      // Set target speeds
      setPanSpeed(targetPanSpeed);
      setTiltSpeed(targetTiltSpeed);
      
      // Acknowledge command
      Serial.print("Set speeds: Pan=");
      Serial.print(targetPanSpeed);
      Serial.print(", Tilt=");
      Serial.println(targetTiltSpeed);
    }
    // Single character commands for testing
    else if (command.length() == 1) {
      char cmd = command.charAt(0);
      switch (cmd) {
        case 'R': setPanSpeed(-MAX_SPEED); break;
        case 'L': setPanSpeed(MAX_SPEED); break;
        case 'U': setTiltSpeed(MAX_SPEED); break;
        case 'D': setTiltSpeed(-MAX_SPEED); break;
        case 'S': 
          setPanSpeed(0);
          setTiltSpeed(0);
          break;
      }
    }
  }
  if (gpsSerial.available() > 0)
    if (gps.encode(gpsSerial.read()))
      displayInfo();

  
  
  // Update servo positions based on current speeds
  updateServos();
  
}

void setPanSpeed(int targetSpeed) {
  panSpeed = constrain(targetSpeed, -MAX_SPEED, MAX_SPEED);
}

void setTiltSpeed(int targetSpeed) {
  tiltSpeed = constrain(targetSpeed, -MAX_SPEED, MAX_SPEED);
}

void updateServos( ) {
  // Only update at specified intervals for smoother motion
  unsigned long currentTime = millis();
  if (currentTime - lastUpdateTime >= UPDATE_INTERVAL) {
    // Update pan servo
    panServo.write(PAN_STOP - panSpeed);
    
    // Update tilt servo
    tiltServo.write(TILT_STOP - tiltSpeed);
    
    lastUpdateTime = currentTime;
  }

}

void displayInfo()
{
  if (gps.location.isValid())
  {
    Serial.print("Latitude: ");
    Serial.println(gps.location.lat(), 6);
    Serial.print("Longitude: ");
    Serial.println(gps.location.lng(), 6);
    Serial.print("Altitude: ");
    Serial.println(gps.altitude.meters());
  }
  else
  {
    Serial.println("Location: Not Available");
  }
  
  Serial.print("Date: ");
  if (gps.date.isValid())
  {
    Serial.print(gps.date.month());
    Serial.print("/");
    Serial.print(gps.date.day());
    Serial.print("/");
    Serial.println(gps.date.year());
  }
  else
  {
    Serial.println("Not Available");
  }

  Serial.print("Time: ");
  if (gps.time.isValid())
  {
    if (gps.time.hour() < 10) Serial.print(F("0"));
    Serial.print(gps.time.hour());
    Serial.print(":");
    if (gps.time.minute() < 10) Serial.print(F("0"));
    Serial.print(gps.time.minute());
    Serial.print(":");
    if (gps.time.second() < 10) Serial.print(F("0"));
    Serial.print(gps.time.second());
    Serial.print(".");
    if (gps.time.centisecond() < 10) Serial.print(F("0"));
    Serial.println(gps.time.centisecond());
  }
  else
  {
    Serial.println("Not Available");
  }

  Serial.println();
  Serial.println();
  delay(1000);
}