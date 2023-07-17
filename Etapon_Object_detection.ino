#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "HUSKYLENS.h"
#include <Arduino.h>
#include <WiFi.h>
#include <NewPing.h>  //Ultrasonic
#include <Firebase_ESP_Client.h>
#include <SoftwareSerial.h>
#include <ESP32Servo.h>
#include <String.h> 
#include "time.h"

// Provide the token generation process info.
#include <addons/TokenHelper.h>

/* 1. Define the WiFi credentials */
#define WIFI_SSID "..."
#define WIFI_PASSWORD "051228092601"

/* 2. Define the API Key */
#define API_KEY "AIzaSyDYtHfiC6sWG-uuwLJDA5wB6sEPirBSybo"

/* 3. Define the project ID */
#define FIREBASE_PROJECT_ID "etapon-63310"

/* 4. Define the user Email and password that alreadey registerd or added in your project */
#define USER_EMAIL "etaponadmin1@gmail.com"
#define USER_PASSWORD "password"

// Define Firebase Data object
FirebaseData fbdo;

FirebaseAuth auth;
FirebaseConfig config;

#define PLASTIC_BOTTLE_ID_1 1
#define BUZZER 13
#define IR_SENSOR_PIN 4
#define DEPOSIT_SENSOR 26
#define TRIGGER_PIN 2  // Arduino pin tied to trigger pin on the ultrasonic sensor.
#define ECHO_PIN 15    // Arduino pin tied to echo pin on the ultrasonic sensor.
#define SERVO_PIN 12

NewPing sonar(TRIGGER_PIN, ECHO_PIN);
HUSKYLENS huskylens;
LiquidCrystal_I2C lcd(0x27, 16, 2);  // Set the LCD I2C address and dimensions
SoftwareSerial mySerial(14, 27);     // RX, TX

const unsigned long interval = 1000;  // Timer interval in milliseconds (5 seconds)
unsigned long previousMillis = 0;

int plasticBottleCount = 0;
bool plasticDetected = false;
bool alertShowed = false;
int plasticDetectedCheck = 0;
int notBottle = 0;
int recentBottle;
int currentBottle = 0;
int level = 0;
double points = 0.0;
double bottlePrice = 0.0;

bool started = false;
bool startScan = false;
bool isScanning = false;
bool scanComplete = false;

bool depositTimer = false;
int depositSeconds = 10;

const int MIN_DISTANCE = 10;   // Replace with your desired minimum distance in centimeters.
const int MAX_DISTANCE = 103; 

bool userValidated = false;
int seconds = 10;           // Variable to store the elapsed seconds
int scanningSeconds = 7;
bool scanningTimer = false;
bool timerRunning = false;  // Flag to indicate if the timer is running

String userID = "";
double pointsEarned = 0.0;

const char* ntpServer = "asia.pool.ntp.org";

// Variable to save current epoch time
unsigned long epochTime; 

void printResult(HUSKYLENSResult result);
void updateLCD();


// Function that gets current epoch time
unsigned long getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    //Serial.println("Failed to obtain time");
    return(0);
  }
  time(&now);
  return now;
}

void okBuzzer() {
  tone(BUZZER, 2000);
  delay(200);
  noTone(BUZZER);
}
void toneBinFull() {
  tone(BUZZER, 400);
  delay(1000);
  noTone(BUZZER);
}

void noBuzzer() {
  tone(BUZZER, 50);
  delay(100);
  noTone(BUZZER);
  delay(250 );
  tone(BUZZER, 50);
  delay(100);
  noTone(BUZZER);
}

void getBottlePoints() {
  FirebaseJson content;
  FirebaseJson pointsContent;

  String pointsPath = "bottleprice/botprice";

  if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", pointsPath.c_str())) {
    FirebaseJson documentData;
    FirebaseJsonData result;

    documentData.setJsonData(fbdo.payload());
    Serial.printf("Received Firebase data:\n%s\n\n", fbdo.payload().c_str());

    if (documentData.get(result, "fields/points")) {
      if (documentData.get(result, "fields/points/doubleValue")) {
        bottlePrice = result.to<double>();
        Serial.print("bottlePrice Value Set (Double): ");
        Serial.println(bottlePrice);
      } else if (documentData.get(result, "fields/points/integerValue")) {
        bottlePrice = result.to<double>();
        Serial.print("bottlePrice Value Set (Integer): ");
        Serial.println(bottlePrice);
      } else {
        Serial.println("Error: 'doubleValue' or 'integerValue' not found in Firebase data.");
      }
    } else {
      Serial.println("Error: 'fields/points' not found in Firebase data.");
    }
  } else {
    Serial.println("Firebase Error: " + fbdo.errorReason());
  }
}



void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  mySerial.begin(115200);
  configTime(0, 0, ntpServer);
  Wire.begin();
  
  turnServo(SERVO_PIN, 0, 0);
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");
  unsigned long ms = millis();
  lcd.init();  // Initialize the LCD
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Please Wait!");
  while (WiFi.status() != WL_CONNECTED)
  {
      Serial.print(".");
      delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION);

  /* Assign the api key (required) */
  config.api_key = API_KEY;

  /* Assign the user sign in credentials */
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;


  /* Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h

  // Limit the size of response payload to be collected in FirebaseData
  fbdo.setResponseSize(2048);

  Firebase.begin(&config, &auth);

  Firebase.reconnectWiFi(true);
  while(bottlePrice <= 0){
    getBottlePoints();
  }
 
  

  pinMode(DEPOSIT_SENSOR, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  // Check Huskylens Connectioni
  while (!huskylens.begin(Wire)) {
    Serial.println(F("Begin failed!"));
    Serial.println(F("1.Please recheck the \"Protocol Type\" in HUSKYLENS (General Settings>>Protocol Type>>I2C)"));
    Serial.println(F("2.Please recheck the connection."));
    delay(100);
  }
  updateBinLevel();

  lcd.clear();
  lcd.setCursor(5,0);
  lcd.print("eTapon");
  Serial.println(bottlePrice);
}  // setup end



void validateUser(String userid){
  userid.trim();
  String usersPath = "users/" + userid;

  if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", usersPath.c_str())){
    if (fbdo.payload().length() > 0) {
      userValidated = true;
      Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
    } else {
      userValidated = false;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("User Not Found");
      delay(1000);
      showBrand();
      Serial.println("Document doesn't exist.");
      resetBin();
    }
  }    
  else{
      Serial.println(fbdo.errorReason());
    }
}




void loop() {

  unsigned long currentMillis = millis();
  int currentBinLevel = sonar.ping_cm();
  delay(100);

  if (!huskylens.request()) {
    Serial.println(F("Fail to request data from HUSKYLENS, recheck the connection!"));
  }
  if(mySerial.available()){
    Serial.print("Serial Available");
    String c = mySerial.readString();
    userID = c;
    userID.trim();
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("ID :");
    lcd.print(userID);
    delay(500);
    if(userID.length() >1){
      validateUser(userID);
      if(userValidated){
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Waiting");
        lcd.setCursor(0, 1);
        lcd.print("for Deposit");
        depositTimer = true;
      }
      else{
        lcd.clear();
        lcd.print("User Not Found");
        delay(2000);
        showBrand();
      }
    }  
  }//end if(mySerial.available)
  if (currentBinLevel <= 20 && started == false) {
    Serial.print(String(currentBinLevel));
    toneBinFull();
    showBinFull();
    delay(5000);
    showBrand();
  }
  // else if (getBinLevel() <= 10 && started == true && plasticDetected == false && isScanning == false) {
  else if (timerRunning == true && currentBinLevel <= 20 && isScanning == false && started == true && plasticDetected == false) {
    Serial.print(String(currentBinLevel));
    timerRunning = false;
    toneBinFull();
    displayLCD("Bin Full");
    delay(1000);
    displayLCD("Points Sending"); 
    epochTime = getTime();
    sendPoints(userID);
    resetBin(); 
  } 
  else if(userValidated){
    if (digitalRead(DEPOSIT_SENSOR) == LOW && getBinLevel() > 20 && isScanning == false && startScan == false) {
      depositTimer = false;
      started = true;
      timerRunning = false;
      seconds = 10;
        delay(500);
        lcd.clear();
        lcd.print("Scanning...");
        startScan = true;
        Serial.print("Scan will Start");
    }  // if sensor detect end

    // Object Detection Start
    if (startScan == true && plasticDetected == false) {
      scanningTimer = true;
      isScanning = true;
      Serial.println("Scanning Start");
      while (huskylens.available() && !plasticDetected) {
        Serial.println(".");
        Serial.print("Scanning");
        HUSKYLENSResult result = huskylens.read(); 
        if(!plasticDetected) {
          if(result.ID == PLASTIC_BOTTLE_ID_1 ){
            plasticDetectedCheck++;
          }else if(result.ID != PLASTIC_BOTTLE_ID_1 ){
            notBottle++;
          }else{
            notBottle++;
          }
          
          if (plasticDetectedCheck == 5) {
            scanningTimer = false;
            plasticDetected = true;
            isScanning = false;
            turnServo(SERVO_PIN, 90, 500);
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Bottle Accepted");
            okBuzzer();
          } else if (notBottle == 5) {
            scanningTimer = false;
            isScanning = false;
            startScan = false;
            noBuzzer();
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Not Accepted");
            plasticDetected = false;
            plasticDetectedCheck = 0;
            notBottle = 0;
            timerRunning = true;
            break;
          }
          delay(10);
        }            // while huskyles end
        delay(200);  // half second delay for validating object
      }              // while end
    }                // if start scan end
  }                  //else end


  // Detect Fall
  if (digitalRead(IR_SENSOR_PIN) == LOW && plasticDetected == true && startScan == true) {
    timerRunning = true;
    startScan = false;
    plasticBottleCount++;
    updateLCD();
    while (digitalRead(IR_SENSOR_PIN) == LOW) {
      // Wait for the plastic bottle to be removed
      delay(200);
    }
    turnServo(SERVO_PIN, 180, 500);
    lcd.clear();
    
    plasticDetected = false;
    plasticDetectedCheck = 0;
    currentBottle = 0;
    notBottle = 0;

  }  // if ir sensor end

  if (depositTimer) {
      lcd.setCursor(0, 0);  // Set the cursor to the first row
      lcd.print("Scanning");
    if (currentMillis - previousMillis >= interval) {
      delay(200);
      // Display the timer on the LCD
      previousMillis = currentMillis;
      // lcd.setCursor(0,1);
      // lcd.print("Time Left: ");
      // lcd.print(scanningSeconds);
      // lcd.print(" ");
      depositSeconds--;
    }
  }
  if(depositSeconds == 0){
    depositTimer = false;
    lcd.clear();
    displayLCD("No Deposit");
    delay(1000);
    displayLCD("Transaction End");
    delay(500);
    resetBin();

  }
  if (scanningTimer) {
      lcd.setCursor(0, 0);  // Set the cursor to the first row
      lcd.print("Scanning");
    if (currentMillis - previousMillis >= interval) {
      delay(200);
      // Display the timer on the LCD
      previousMillis = currentMillis;
      // lcd.setCursor(0,1);
      // lcd.print("Time Left: ");
      // lcd.print(scanningSeconds);
      // lcd.print(" ");
      scanningSeconds--;
    }
  }

  if(scanningSeconds == 0){
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Cant Detect");
    isScanning=false;
    scanningTimer=false;
    startScan = false;
    timerRunning = true;
    scanningSeconds=7;
    delay(1000);
  }

  if (timerRunning) {
      lcd.setCursor(0, 0);  // Set the cursor to the first row
      lcd.print("Deposit Another?");
    if (currentMillis - previousMillis >= interval) {
      delay(200);
      // Display the timer on the LCD
      previousMillis = currentMillis;
      lcd.setCursor(0,1);
      lcd.print("Time Left: ");
      lcd.print(seconds);
      lcd.print(" ");
      seconds--;
    }
  }

  if (seconds == 0 && plasticBottleCount >= 1) {
    timerRunning = false;  // Stop the timer
    delay(300);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Points sending...");
    delay(200);
    epochTime = getTime();
    sendPoints(userID); // Start scanning the QR code
    resetBin();
  }else if(seconds == 0 && plasticBottleCount == 0){
    timerRunning = false;  // Stop the timer
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Transaction End");
    delay(2000);
    resetBin();
  }

}


void displayLCD(String message){
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(message);
}

//Get bin level
int getBinLevel() {
  int binlevel = sonar.ping_cm();
  return binlevel;
}

void showBinFull() {
  lcd.clear();
  lcd.setCursor(4, 0);
  lcd.print("Bin Full");
  lcd.setCursor(0, 1);
  delay(1000);
}

double computePoints(){
  return double(plasticBottleCount) * bottlePrice;
}


void createLog(String uid){
    uid.trim();
    FirebaseJson content;
    FirebaseJson pointsContent;
  
    String path = "bin_log";
    pointsEarned = computePoints();
    delay(100);
    content.clear();
    content.set("fields/points/doubleValue", pointsEarned);
    content.set("fields/totalBottle/integerValue", plasticBottleCount);
    content.set("fields/userID/stringValue", uid);
    content.set("fields/createdAt/timestampValue", getRfc3339Timestamp(epochTime));

     Serial.print("Creating Log... ");

    if (Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "" /* databaseId can be (default) or empty */, path.c_str(), content.raw()))
        Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
    else
        Serial.println(fbdo.errorReason());
}




void createTransaction(String uid){
    uid.trim();
    FirebaseJson content;
    FirebaseJson pointsContent;
    // String userTransactionPath = "users/"+uid;
    String path = "users/"+ uid + "/transactions";
    delay(100);
    Serial.print(path);
    pointsEarned = computePoints();
    
    Serial.print(epochTime);
    content.clear();
    content.set("fields/points/doubleValue", double(pointsEarned));
    content.set("fields/totalBottles/integerValue", plasticBottleCount);
    content.set("fields/type/stringValue", "Earned Points");
    content.set("fields/createdAt/timestampValue", getRfc3339Timestamp(epochTime));
    Serial.print("Creating Transaction... ");

    if (Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "" /* databaseId can be (default) or empty */, path.c_str(), content.raw()))
        Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
    else
        Serial.println(fbdo.errorReason());

}

void sendPoints(String uid){
    uid.trim();
    pointsEarned = computePoints();
    FirebaseJson content;
    FirebaseJson pointsContent;

    String usersPath = "users/" + uid;
    delay(200);
    

    if(Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", usersPath.c_str())){
      FirebaseJson documentData;
      FirebaseJsonData result;

      documentData.setJsonData(fbdo.payload());
      Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
      documentData.get(result, "fields/points/doubleValue");
      documentData.get(result, "fields/points/integerValue");
      if(result.success){
        points = result.to<double>() + pointsEarned;
        pointsContent.set("fields/points/doubleValue", points);

        Serial.print("Sending Points... ");

        if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "" /* databaseId can be (default) or empty */, usersPath.c_str(), pointsContent.raw(), "points"))
        Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
        else
        Serial.println(fbdo.errorReason());
        createTransaction(uid);
        createLog(uid);
      }
      else
        Serial.print("error");
    }
    
}


void turnServo(int servoPin, int degrees, int duration) {
  // Attach the servo to the specified pin.
  Servo myservo;
  myservo.attach(servoPin);

  // Set the servo to the desired position.
  myservo.write(degrees);

  // Wait for the specified duration.
  delay(duration);

  // Detach the servo from the pin.
  myservo.detach();
}



void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Plastic Bottles:");
  lcd.setCursor(0, 1);
  lcd.print(plasticBottleCount);
}


void updateBinLevel(){
  // Firebase.ready() should be called repeatedly to handle authentication tasks.
    level = getBinLevel();

    int newLevel = map(level, MIN_DISTANCE, MAX_DISTANCE, 0, 100); // Map the measured distance to the range of 0 to 100.
    newLevel = 100 - constrain(newLevel, 0, 100); // Ensure the value is within the desired range.
   
    FirebaseJson content;
   
    String documentPath = "bin_level/level";
    content.clear();
    content.set("fields/binlevel/integerValue", String(newLevel).c_str());
   

    Serial.print("Updating Bin Level... ");

    /** if updateMask contains the field name that exists in the remote document and
      * this field name does not exist in the document (content), that field will be deleted from remote document
      */

    if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "" /* databaseId can be (default) or empty */, documentPath.c_str(), content.raw(), "binlevel" /* updateMask */))
        Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
    else
        Serial.println(fbdo.errorReason());
}


String getRfc3339Timestamp(time_t epochTime) {
  char buffer[30];
  struct tm* timeinfo;
  timeinfo = gmtime(&epochTime);
  strftime(buffer, 30, "%Y-%m-%dT%H:%M:%SZ", timeinfo);
  return String(buffer);
} 

void resetBin(){
    updateBinLevel();
    showBrand();
    pointsEarned = 0;
    started = false;
    isScanning = false;
    startScan = false;
    plasticBottleCount=0;
    plasticDetected = false;
    plasticDetectedCheck = 0;
    currentBottle = 0;
    notBottle = 0;
    userID = "";
    scanComplete = false;
    seconds = 10;
    userValidated = false;
    depositSeconds=10;
}

void reset(){
    showBrand();
    pointsEarned = 0;
    started = false;
    isScanning = false;
    startScan = false;
    plasticBottleCount=0;
    plasticDetected = false;
    plasticDetectedCheck = 0;
    currentBottle = 0;
    notBottle = 0;
    userID = "";
    scanComplete = false;
    seconds = 10;
    userValidated = false;
}

void showBrand(){
  lcd.clear();
  lcd.setCursor(5,0);
  lcd.print("eTapon");
}

