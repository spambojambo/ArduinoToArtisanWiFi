#include <MAX31855.h> // Include MAX31855 Sensor library
#include <ArduinoRS485.h> // ArduinoModbus depends on the ArduinoRS485 library
#include <ArduinoModbus.h>
#include <WiFiNINA.h> //nano iot 33 wifi library
#include "arduino_secrets.h"
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h> //OLED library
#include <Adafruit_SSD1306.h> //OLED library
#include <Arduino_LSM6DS3.h> //gyroscope library for Nano 33 IoT

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

//How many values to include in calculating moving average of drumspeed
#define DRUMSPEED_MOVINGAVERAGESIZE 10 

/*******************************************************************************************************************
** program constants                                                                                  **
*******************************************************************************************************************/
const uint32_t SERIAL_SPEED     = 9600; ///< Set the baud rate for Serial I/O
const uint8_t  SPI_SYSTEM_CLOCK =    4; ///< System Clock PIN for SPI
const uint8_t  SPI_CHIP_SELECT  =      5; ///< Chip-Select PIN for SPI
const uint8_t  SPI_MISO         =   6; ///< Master-In, Slave-Out PIN for SPI

/*******************************************************************************************************************
** global variables and instantiate classes                                                               **
*******************************************************************************************************************/
MAX31855_Class MAX31855; ///< instance of MAX31855 (thermocouple board)

///////Wifi keys.sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;        // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)
int keyIndex = 0;                 // your network key Index number (needed only for WEP)


//Wifi server object
int status = WL_IDLE_STATUS;
WiFiServer wifiServer(502);

//Modbus server object
ModbusTCPServer modbusTCPServer;

/*******************************************************************************************************************
** Setup                                                                                                          **
*******************************************************************************************************************/
void setup()
{
  boolean displayState = true; //lets me use logic below to turn off the OLED display when the drum is spinning... if this is false, my functions that write to OLED will turn it off instead.
  
  // Power up OLED display -- SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
      Serial.println(F("SSD1306 allocation failed"));
      for (;;); // Don't proceed, loop forever
    }

  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
    display.display();
    delay(500); // Pause for 2 seconds
    // Clear the buffer
    display.clearDisplay();

  //open serial connection to write to serial
    Serial.begin(SERIAL_SPEED);
    #ifdef  __AVR_ATmega32U4__  // If this is a 32U4 processor, then wait 3 seconds for the interface to initialize
      delay(3000);
    #endif

  //initialize thermocouple board
    Serial.print(F("Initializing MAX31855 sensor\n"));
    printToOLEDDisplay("Initializing MAX31855 sensor", displayState); //write to OLED Display
    while (!MAX31855.begin(SPI_CHIP_SELECT, SPI_MISO, SPI_SYSTEM_CLOCK)) // Software SPI for MAX31855
    {
      Serial.println(F("Unable to start MAX31855. Waiting 3 seconds."));
      printToOLEDDisplay("Unable to start MAX31855. Waiting 3 seconds.",displayState);
      delay(3000);
    } // loop until device is located

  // attempt to connect to Wifi network:
    while (status != WL_CONNECTED) {
      Serial.print("Attempting to connect to SSID: ");
      String tmpString = "Attempting to connect to SSID:";
      printToOLEDDisplay(tmpString+ssid,displayState);
      Serial.println(ssid);
      // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
      status = WiFi.begin(ssid, pass);
  
      // wait 10 seconds for connection:
      delay(10000);
    }

  //Update OLED with status:
    printToOLEDDisplay("Success!", displayState);
    printWifiStatus(displayState);

  // start the Wifi server
    printToOLEDDisplay("Starting wifiServer",displayState); 
    wifiServer.begin();

  // start the Modbus TCP server
    if (!modbusTCPServer.begin()) {
      Serial.println("Failed to start Modbus TCP Server!");
      printToOLEDDisplay("Failed to start Modbus TCP Server!",displayState);
      while (1);
    }

  // configure 4 Modbus registers, starting at address 0x00
    modbusTCPServer.configureHoldingRegisters(0x00, 4); //start at 0x00 and create 4 registers
    modbusTCPServer.holdingRegisterWrite(0x00,0); //BT at 0x00, set to 0 by default
    modbusTCPServer.holdingRegisterWrite(0x01,0); //Air config at 0x01, set to 0 by defualt (sent from Artisan)
    modbusTCPServer.holdingRegisterWrite(0x02,0); //DrumSpeed at 0x02, set to 0 by default
    modbusTCPServer.holdingRegisterWrite(0x03,0); //ArduinoTemp at 0x03, set to 0 by default
    printToOLEDDisplay("modbus wifiServer started",displayState); 

  //check IMU/gyro
    if (!IMU.begin()) {
      Serial.println("Failed to initialize IMU!");
      while (1);
    }
  
} // of method setup()

/***************************************************************************************************************//*!
  @brief    Arduino method for the main program loop
  @details  This is the main program for the Arduino IDE, it is an infinite loop and keeps on repeating.
  @return   void
*******************************************************************************************************************/
void loop()
{
  boolean displayState = true;//make sure display is enabled after client disconnects and loop restarts
  
  //check if disconnected from wifi... try reconnecting if disconnected
    while (status != WL_CONNECTED) {
      Serial.print("Attempting to connect to SSID: ");
      String tmpString = "Attempting to connect to:";
      printToOLEDDisplay(tmpString + ssid, displayState);
      Serial.println(ssid);
      // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
      status = WiFi.begin(ssid, pass);
  
      // wait 10 seconds for connection:
      delay(10000);
    }
  

  // listen for incoming Modbus clients (i.e., Artisan)
    printAwaitingClients(displayState);
    WiFiClient client = wifiServer.available();
    if (client) {
      // a new client connected
          Serial.println("new client");
      //printToOLEDDisplay("Client Detected", displayState); 
      modbusTCPServer.accept(client);
    }

  //indexes used to flip the OLED orentiation every so often to avoid burn-in
    int refreshDisplayIndex = 0;  
    int burnInIndex = 0;
    int burnInRotationIndex=1;
  
  //create array for drumspeed moving average, populate with 0's to start
    int drumSpeedMovingAverage[DRUMSPEED_MOVINGAVERAGESIZE];
    for(int i=0;i < DRUMSPEED_MOVINGAVERAGESIZE; i++){
      drumSpeedMovingAverage[i] = 0 ;  
    }
    int drumSpeedMovingAverageIndex=0;
  

/////Major sub-loop once Artisan is connected 
  while (client.connected()) {
    // poll for Modbus TCP requests, while client connected
    int32_t arduinoTemperature = MAX31855.readAmbient()/100; // retrieve MAX31855 die ambient temperature
    int32_t probeTemperature   = MAX31855.readProbe()/100;   // retrieve thermocouple probe temp
    
    //debug - write out probetemp to serial every loop
    //Serial.print("ProbeTemp is:");
    //Serial.println(probeTemperature);
    
    
    //check for thermocouple faults/errors
    uint8_t faultCode          = MAX31855.fault();       // retrieve any error codes
    if ( faultCode )                                     // Display error code if present
    {
      Serial.print("Fault code ");
      Serial.print(faultCode);
      Serial.println(" returned.");
      printToOLEDDisplay("Fault Code:"+faultCode, displayState);
      modbusTCPServer.holdingRegisterWrite(0x00,0);
      modbusTCPServer.poll();
    }
    else
    {
      modbusTCPServer.holdingRegisterWrite(0x00, (uint16_t)(probeTemperature)); //update modbus register with thermocouple temp
      modbusTCPServer.holdingRegisterWrite(0x03, (uint16_t)(arduinoTemperature)); //update modbus register with MAX31855 temp in the project box
      
      //grab gyro data for drum speed
        float x, y, z;
        if (IMU.gyroscopeAvailable()) {
          IMU.readGyroscope(x,y,z);
          uint16_t drumRPM = (uint16_t) abs(z/6); //z/6 is the conversion from degrees/sec to rev/min
          if (drumSpeedMovingAverageIndex >= DRUMSPEED_MOVINGAVERAGESIZE){
            drumSpeedMovingAverageIndex=0;
          }
          drumSpeedMovingAverage[drumSpeedMovingAverageIndex]=drumRPM;
          drumSpeedMovingAverageIndex = drumSpeedMovingAverageIndex + 1;
          uint16_t drumMovingAverageRPM = movingAverageCalc(drumSpeedMovingAverage,DRUMSPEED_MOVINGAVERAGESIZE);
          modbusTCPServer.holdingRegisterWrite(0x02,drumMovingAverageRPM); //update modbus register with Average drumspeed.)
          
          //////DISABLE OLED WHEN DRUM IS SPINNING    
          if(drumMovingAverageRPM >15){
            displayState = false;
          }
            else{
              displayState = true;
            }//endif
        }
        else
          {Serial.println("nogyro");}
        //endif
          
        modbusTCPServer.poll(); //check for any requests from Artisan (poll often so Artisan doesn't timout)
        
      //refresh display every 10 loops
        if (refreshDisplayIndex>9){
          display.clearDisplay();
          printWifiStatus(displayState); 
          
          //DEBUG - Print each register's value to Serial
          for(int address = 0x00; address < 0x04; address++)
          {
              Serial.print("Modbus register ");
              Serial.print(address);
              Serial.print(": ");
              Serial.println(modbusTCPServer.holdingRegisterRead(address));
          }
          refreshDisplayIndex=0; //reset counter
          modbusTCPServer.poll();//checking for artisan requests to avoid timeouts in case the display changes caused a delay
        } //end if
  
      //flip display orientation every 601 loops to avoid burnin.. arbitrary number because I'm lazy
        if (burnInIndex>600){
          if (burnInRotationIndex=1){
            display.setRotation(2);
            burnInRotationIndex++;
          }
          else {
           display.setRotation(0);
           burnInRotationIndex=1;
          }    
       } //end if
  
        //increment display-refresh counter and burnIn conuter
          refreshDisplayIndex++;
          burnInIndex++;
    }//end else
  } /////end of major subloop

//reconnect to wifi if needed... I forgot why I put this at the end and beginning of loop, and I'm lazy/scared to fix it
//make sure we're still connected to wifi
    while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    String tmpString = "Reconnecting to SSID:";
    printToOLEDDisplay(tmpString+ssid, displayState);
    Serial.println(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    status = WiFi.begin(ssid, pass);

    // wait 10 seconds for connection:
    delay(10000);
  }
} // of method loop()


void printToOLEDDisplay(String screenText, boolean displayState){
 //check if screen should be disabled  
   if (displayState == 0){
     display.clearDisplay();
     display.ssd1306_command(0xAE);
     Serial.println("printToOLEDDISPLAY- DISPLAY OFF");
     return;
  }else{
    display.ssd1306_command(0xAF);
         Serial.println("printToOLEDDISPLAY- DISPLAY On");
  }
 //function to write text to OLED
  display.clearDisplay();
  display.setTextSize(1);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font
  display.println(screenText);
  display.display();
  delay(2000);  
}

void printAwaitingClients(boolean displayState){
  //check if screen should be disabled  
    if (displayState ==0){
     display.clearDisplay();
          display.ssd1306_command(0xAE);
     Serial.println("printAwaitingClients- DISPLAY OFF");
     return;
  } 
  else {     display.ssd1306_command(0xAF);}
  
  //function to update OLED with temps and show that no clients are connected
  int32_t arduinoTemperature = MAX31855.readAmbient(); // retrieve MAX31855 die ambient temperature
  int32_t probeTemperature   = MAX31855.readProbe();   // retrieve thermocouple probe temp
    
  display.clearDisplay();
  display.setTextSize(1);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font
  display.println("Awaiting Clients on:");
  display.print("   ");
  display.println(WiFi.localIP());
      
  display.setCursor(0, 24);
  display.print((float)probeTemperature / 1000 * 9 / 5 + 32, 1);
  display.print(" ""F  |  ");
  display.print((float)probeTemperature / 1000, 1);
  display.print("C");
  display.display(); 
}//end function

float movingAverageCalc (int * array, int len){
 //moving average of all values in an array
  long sum = 0;  // sum will be larger than an item, long for safety.
  for (int i = 0 ; i < len ; i++)
    sum += array [i] ;
  return  ((float) sum) / len ;
}

void printWifiStatus(boolean displayState) {
 //check if screen should be disabled  
  if (displayState ==0){
     display.clearDisplay();
          display.ssd1306_command(0xAE);
     Serial.println("printWifiStatus- DISPLAY OFF");
     return;
  } else {
         display.ssd1306_command(0xAF);
              Serial.println("printWifiStatus- DISPLAY ON");
  }

  //function to print IP Address and Wifi signal strength
  int32_t arduinoTemperature = MAX31855.readAmbient(); // retrieve MAX31855 die ambient temperature
  int32_t probeTemperature   = MAX31855.readProbe();   // retrieve thermocouple probe temp
  // print the SSID of the network you're attached to:
  display.clearDisplay();
  display.setTextSize(1);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font
  long rssi = WiFi.RSSI();
  
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  // display.print("SSID: ");
  //display.print(WiFi.SSID());

  // print your WiFi shield's IP address:
  display.println("Client connected!");
  display.print("   ");
  display.print(WiFi.localIP());
  display.print("  ");
  display.println(rssi);
      
  display.setCursor(0, 24);
  display.print((float)probeTemperature / 1000 * 9 / 5 + 32, 1);
  display.print(" ""F  |  ");
  display.print((float)probeTemperature / 1000, 1);
  display.print("C");
  display.display();

  // print the received signal strength:
  rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
} //end function
