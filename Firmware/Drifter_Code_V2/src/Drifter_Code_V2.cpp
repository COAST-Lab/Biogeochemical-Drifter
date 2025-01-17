/* 
 * Project Biogeochemical Drifter
 * Author: Stephen Lail
 * Date: 6/20/24
 * For comprehensive documentation and examples, please visit:
 * https://docs.particle.io/firmware/best-practices/firmware-template/
 */

// Include Particle Device OS APIs
#include "Particle.h"

void printToFile();
void serialPrintGPSTime();
void serialPrintGPSLoc();
long real_time;
int millis_now;
float get_last_received_reading();
float last_ph = 0.0;
float last_rtd = 0.0;
float last_do = 0.0;
float last_ec = 0.0;

//--------Scan for i2c devices---------
void scanI2CDevices() {
    byte error, address;
    int nDevices;

    Serial.println("Scanning for I2C devices...");
    nDevices = 0;

    for (address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();

        if (error == 0) {
            Serial.print("I2C device found at address 0x");
            if (address < 16)
                Serial.print("0");
            Serial.print(address, HEX);
            Serial.println(" !");
            nDevices++;
        } else if (error == 4) {
            Serial.print("Unknown error at address 0x");
            if (address < 16)
                Serial.print("0");
            Serial.println(address, HEX);
        }
    }

    if (nDevices == 0)
        Serial.println("No I2C devices found.\n");
    else
        Serial.println("Done scanning.\n");
}
//--------SD configuration------------

#include <SdFat.h>
const int SD_CHIP_SELECT = D5;
SdFat SD;

char filename[] = "YYMMDD00.csv"; // filename in year, month, day, 00-99 file number
bool filenameCreated = false;

//-------GPS---------------------------------

#include <Adafruit_GPS.h>
#define GPSSerial Serial1
Adafruit_GPS GPS( &GPSSerial);
uint32_t timer = millis();

//------Sensor Array----------------------------

//#include "Ezo_I2c_lib-master.h"
#include <Wire.h>
#include <Ezo_i2c.h>
#include <Ezo_i2c_util.h>
#include <iot_cmd.h>
#include <sequencer4.h>
void step1();//Read RTD circuit
void step2();//temperature compensation
void step3();//send a read command
void step4();// print data to serial monitor and to SD card
void receive_reading(Ezo_board & Sensor);
void publishData();

Ezo_board ph = Ezo_board(99, "PH");
Ezo_board rtd = Ezo_board(102, "TEMP");
Ezo_board DO = Ezo_board(97, "DO");
Ezo_board ec = Ezo_board(100, "EC");

Sequencer4 Seq(&step1, 300, &step2, 300, &step3, 900, &step4, 500);

//------LED Light-----------------

const pin_t MY_LED = D7;
bool led_state = HIGH;

SYSTEM_MODE(SEMI_AUTOMATIC);
SYSTEM_THREAD(ENABLED);
unsigned long lastPublishTime = 0;  // Track when we last sent data
const unsigned long publishInterval = 300000;  // 5 minutes (in milliseconds)
void setup(){
    pinMode(MY_LED, OUTPUT);
    Wire.begin();
    Seq.reset();

    Cellular.off();
    //Cellular.connect();
     
    // Wait until the cellular connection is established
    delay(1000);

    Serial.begin(9600);
    Serial.println("Adafruit GPS Sensor Test");

    GPS.begin(9600);

    GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);

    GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);

    if (!SD.begin(SD_CHIP_SELECT, SPI_FULL_SPEED)){
        Serial.println("failed to open card");
        return;
    }
     scanI2CDevices(); //Scan for I2C devices
// Particle.connect();
//  while (!Particle.connected()) {
//     // Wait until the Particle cloud connection is established
//     delay(1000);
//   }
//   Particle.publish("Cellular connected and Publishing", "Success", PRIVATE);

}
void loop(){

    Seq.run();
    GPS.read();


    if (GPS.newNMEAreceived()){
        if (!GPS.parse(GPS.lastNMEA()))
        return;
    }
    if (millis() - timer > 2000){
        timer = millis();
        serialPrintGPSTime();

        if (GPS.fix){
            led_state = !led_state;
            digitalWrite(MY_LED, led_state);
            serialPrintGPSLoc();

            Serial.println("Reached latitude and longitude print statements");

            Serial.print("Latitude: ");
            Serial.print(GPS.latitude, 6); // Print latitude with 6 decimal places
            Serial.print(" ");
            Serial.print(GPS.lat);

            Serial.print(" Longitude: ");
            Serial.print(GPS.longitude, 6);
            Serial.print(" ");
            Serial.println(GPS.lon);

        }
        printToFile();

    }
    
//     if (millis() - lastPublishTime >= publishInterval) {
//     publishData();
//   }

}

// ---------------Steps for EZO sensors------------

void step1(){
    rtd.send_read_cmd(); //read the RTD circuit

}

void step2(){
    if ((rtd.get_error() == Ezo_board::SUCCESS) && (rtd.get_last_received_reading() > -1000.0)){ //If temperature reading has been received and is valid
     ph.send_cmd_with_num("T,", rtd.get_last_received_reading());
     ec.send_cmd_with_num("T,", rtd.get_last_received_reading());
     DO.send_cmd_with_num("T,", rtd.get_last_received_reading());
    }
    else{
        ph.send_cmd_with_num("T,", 25.0);
        ec.send_cmd_with_num("T,", 25.0);
        DO.send_cmd_with_num("T,", 25.0);
    }
}

void step3(){
    ph.send_read_cmd();
    ec.send_read_cmd();
    DO.send_read_cmd();
}

void step4(){
    // RTD (temperature)
     receive_and_print_reading(rtd);
    if (rtd.get_error() == Ezo_board::SUCCESS) {
        last_rtd = rtd.get_last_received_reading();  // Store latest RTD reading
        Serial.print("RTD: ");
        Serial.println(last_rtd);
    } else {
        Serial.print("RTD reading error: ");
        Serial.println(rtd.get_error());
    }

    // pH
    receive_and_print_reading(ph);
    if (ph.get_error() == Ezo_board::SUCCESS) {
        last_ph = ph.get_last_received_reading();  // Store latest pH reading
        Serial.print("pH: ");
        Serial.println(last_ph);
    } else {
        Serial.print("pH reading error: ");
        Serial.println(ph.get_error());
    }

    // EC
    receive_and_print_reading(ec);
    if (ec.get_error() == Ezo_board::SUCCESS) {
        last_ec = ec.get_last_received_reading();  // Store latest EC reading
        Serial.print("EC: ");
        Serial.println(last_ec);
    } else {
        Serial.print("EC reading error: ");
        Serial.println(ec.get_error());
    }

    // DO
    receive_and_print_reading(DO);
    if (DO.get_error() == Ezo_board::SUCCESS) {
        last_do = DO.get_last_received_reading();  // Store latest DO reading
        Serial.print("DO: ");
        Serial.println(last_do);
    } else {
        Serial.print("DO reading error: ");
        Serial.println(DO.get_error());
    }
    Serial.println();
}
void receive_reading(Ezo_board & Sensor){
    Serial.print(Sensor.get_name()); Serial.print(": ");
    Serial.print(Sensor.receive_read_cmd());

    switch (Sensor.get_error()){
        case Ezo_board::SUCCESS:
          Serial.print(Sensor.get_last_received_reading());
          break;

        case Ezo_board::FAIL:
          Serial.print("Failed ");
          break;

        case Ezo_board::NOT_READY:
          Serial.print("Pending ");
          break;

        case Ezo_board::NO_DATA:
          Serial.print("No Data ");
          break;


    }
}
// ---------------Print to SD Card--------------
void printToFile(){

    if (!filenameCreated){
        //Year, month, day for filename
        int filenum = 0; //start at zero and increment by one if file exists
        sprintf(filename, "%02d%02d%02d%02d.csv", GPS.year, GPS.month, GPS.day, filenum);

        while (SD.exists(filename)) {
            filenum++;
            sprintf(filename, "%02d%02d%02d%02d.csv", GPS.year, GPS.month, GPS.day, filenum);

        }
        filenameCreated = true;
    }
    Serial.println(filename);

    File dataFile = SD.open(filename, FILE_WRITE);

    if(dataFile){
       //Date
        dataFile.print(GPS.month, DEC);
        dataFile.print('/');
        dataFile.print(GPS.day, DEC);
        dataFile.print("/20");
        dataFile.print(GPS.year, DEC);
        dataFile.print(",");
        //Time
        dataFile.print(GPS.hour, DEC);
        dataFile.print(':');
        if (GPS.minute < 10){
            dataFile.print('0');
        }
        dataFile.print(GPS.minute, DEC);
        dataFile.print(':');
        if(GPS.seconds <10){
            dataFile.print('0');
        }
        dataFile.print(GPS.seconds, DEC);
        dataFile.print(".");
        if(GPS.milliseconds < 10){
            dataFile.print("00");
        } else if (GPS.milliseconds >9 && GPS.milliseconds <100){
            dataFile.print("0");
        }
        dataFile.print(GPS.milliseconds);
        dataFile.print(",");

        //Elapsed time
        dataFile.print(millis()/1000);
        dataFile.print(",");

        //Location
        dataFile.print(GPS.latitude, 5);
        dataFile.print(",");
        dataFile.print(GPS.lat);
        dataFile.print(",");
        dataFile.print(GPS.longitude, 5);
        dataFile.print(",");
        dataFile.print(GPS.lon);
        dataFile.print(",");

        //Altitude
        dataFile.print(GPS.altitude);
        dataFile.print(",");
        dataFile.print(GPS.speed);
        dataFile.print(",");

        //Angle
        dataFile.print(GPS.angle);
        dataFile.print(",");

        //pH
        dataFile.print(ph.get_last_received_reading());
        dataFile.print(",");

        //rtd
        dataFile.print(rtd.get_last_received_reading());
        dataFile.print(",");

        //DO
        dataFile.print(DO.get_last_received_reading());
        dataFile.print(",");

        //EC
        dataFile.print(ec.get_last_received_reading());
        dataFile.println();


        dataFile.close();

        
        // //pH
        // dataFile.print(ph);
        // dataFile.print(",");

        // //rtd
        // dataFile.print(rtd);
        // dataFile.print(",");

        // //DO
        // dataFile.print(DO);
        // dataFile.print(",");

        // //EC
        // dataFile.print(ec);
        // dataFile.println;





       


    }
    else{
        Serial.println("error opening datalog.txt");
    }
}
void serialPrintGPSTime(){
    Serial.print("\nTime");

    //Hour
    if (GPS.hour < 10){
        Serial.print('0');

    }
    Serial.print(GPS.hour, DEC);
    Serial.print(':');

    //Minute
    if (GPS.minute <10){
        Serial.print('0');
    }
    Serial.print(GPS.minute, DEC);
    Serial.print(':');

    //Seconds
    if (GPS.seconds < 10) {
    Serial.print('0');
  }
  Serial.print(GPS.seconds, DEC);
  Serial.print('.');
  if (GPS.milliseconds < 10) {
    Serial.print("00");
  } else if (GPS.milliseconds > 9 && GPS.milliseconds < 100) {
    Serial.print("0");
  }
  Serial.println(GPS.milliseconds);
  
  Serial.print("Date: ");
  Serial.print(GPS.month, DEC);
  Serial.print('/');
  Serial.print(GPS.day, DEC);
  Serial.print("/20");
  Serial.println(GPS.year, DEC);
  
  Serial.print("Fix: ");
  Serial.print((int) GPS.fix);
  
  Serial.print(" quality: ");
  Serial.println((int) GPS.fixquality);


}

void serialPrintGPSLoc() {
  Serial.print("Location: ");
  Serial.print(GPS.latitude, 6);
  Serial.print(GPS.lat);
  Serial.print(", ");
  Serial.print(GPS.longitude, 6);
  Serial.println(GPS.lon);
}
// void publishState(){
//     bool isMaxTime = false;
//     stateTime = millis();

//     while (!isMaxTime){
//         if(!Particle.connected()){
//             Particle.connect();
//             Log.info("Trying to connect");
//         }
//         if (Particle.connected()){
//             Log.info("publishing data");
//             snprintf(data, sizeof(data), "%li,%f,%f,%f,%f,%f,%f",
//             real_time,
//             last_lat,
//             last_lon,
//             last_rtd,
//             last_ph,
//             last_ec,
//             last_do

//             );

//             delay(2000);

//             bool success = Particle.publish(data, PRIVATE, WITH_ACK);
//             Log.info("publish result %d", success);

//             delay(2000);

//             isMaxTime = true;
//             state = SLEEP_STATE;
//         } else {
//             if (millis() - stateTime >= MAX_TIME_TO_PUBLISH_MS) {
//                 isMaxTime = true;
//                 state = SLEEP_STATE;
//                 Log.info("max time for publishing reached without success: go to sleep");

//             }
//             Log.info("Not max time, try again to connect and publish");
//             delay(500);
            
//         }
    
//         }
//     }
// }
void publishData() {
    // Format the data as a string with sensor readings
    String data = String::format(
        "Time: %d, GPS_Lat: %li, GPS_Lon: %li,pH: %.2f, RTD (Temp): %.2f°C, DO: %.2f mg/L, EC: %.2f µS/cm",
        Time.now(), GPS.latitude, GPS.longitude, last_rtd, last_do, last_ec
    );

    // Publish the data to the Particle Cloud
    Particle.publish("sensor-data", data, PRIVATE);

    // Log the time of this publish event
    lastPublishTime = millis();
    
    // Print the data to the serial monitor for debugging
    Serial.println("Published Data: " + data);
}