#include <CayenneLPP.h>
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <Wire.h>
#include <math.h>



// LoRaWAN NwkSKey, network session key
static const PROGMEM u1_t NWKSKEY[16] = { 0x3F, 0xF6, 0xDB, 0xCD, 0x44, 0x1C, 0x7C, 0xA1, 0x29, 0x50, 0x78, 0xA5, 0xB1, 0xCF, 0xE7, 0x41 };
// LoRaWAN AppSKey, application session key
static const u1_t PROGMEM APPSKEY[16] = { 0xA6, 0x09, 0x07, 0x89, 0x7E, 0x6C, 0x93, 0x79, 0xFC, 0x09, 0x99, 0x04, 0x35, 0xD2, 0xAD, 0x5D };
// LoRaWAN end-device address (DevAddr)
static const u4_t DEVADDR = 0x26041E0E;
// These callbacks are only used in over-the-air activation, so they are
// left empty here (we cannot leave them out completely unless
// DISABLE_JOIN is set in config.h, otherwise the linker will complain).
void os_getArtEui (u1_t* buf) { }
void os_getDevEui (u1_t* buf) { }
void os_getDevKey (u1_t* buf) { }

static osjob_t sendjob;
// Schedule data trasmission in every this many seconds (might become longer due to duty
// cycle limitations).
// we set 10 seconds interval
const unsigned TX_INTERVAL = 10; // Fair Use policy of TTN requires update interval of at least several min. We set update interval here of 1 min for testing

// Pin mapping according to Cytron LoRa Shield RFM
const lmic_pinmap lmic_pins = {
  .nss = 5,
  .rxtx = LMIC_UNUSED_PIN,
  .rst = 25,
  .dio = {26, 34, 35},
};

CayenneLPP lpp(15);


int readSensor(double* temperature, double * humidity)
{
  Serial.printf("Reading humidity and temperature values ...\r\n");  
  
  //Point to device 0x40 (Address for HDC1080)
  Wire.beginTransmission(0x40);
  //Point to register 0x00 (Temperature Register)
  Wire.write(0x00);
  //Relinquish master control of I2C line
  //pointing to the temp register triggers a conversion
  Wire.endTransmission();
  
  //delay to allow for sufficient conversion time
  delay(20);
  
  //Request four bytes from registers
  Wire.requestFrom(0x40, 4);
  
  //If the 4 bytes were returned sucessfully
  //if (4 <= Wire.available())
  {
    uint16_t rawTemp;
    uint16_t rawHumid;

    Serial.printf("Got 4 bytes read ...\r\n");
    //take reading
    //Byte[0] holds upper byte of temp reading
    rawTemp = rawTemp << 8 | Wire.read();
    //Byte[1] holds lower byte of temp reading
    rawTemp = rawTemp << 8 | Wire.read();
    
    //Byte[3] holds upper byte of humidity reading
    rawHumid = rawHumid << 8 | Wire.read();
    //Byte[4] holds lower byte of humidity reading
    rawHumid = rawHumid << 8 | Wire.read();

    //Temp(C) = reading/(2^16)*165(C) - 40(C)
    *temperature = ((double)(rawTemp)) * 165 / 65536 - 40;

    //Humidity(%) = reading/(2^16)*100%
    *humidity =  ((double)(rawHumid)) * 100/65536;

    return 0;
  }
  return -1;
}

void onEvent (ev_t ev) 
{
  Serial.print(os_getTime());
  Serial.print(": ");
  switch(ev) 
  {
    case EV_TXCOMPLETE:
      Serial.printf("EV_TXCOMPLETE (includes waiting for RX windows)\r\n");
      // Schedule next transmission
      os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(TX_INTERVAL), do_send);
      break;  
    case EV_RXCOMPLETE:
      if (LMIC.dataLen)
      {
        Serial.printf("Received %d bytes\n", LMIC.dataLen);
      }
      break;
    default:
      Serial.printf("Unknown event\r\n");
      break;
  }
}

void do_send(osjob_t* j)
{
  // Check if there is not a current TX/RX job running
  if (LMIC.opmode & OP_TXRXPEND) 
  {
    Serial.printf("OP_TXRXPEND, not sending\r\n");
  } 
  else
  if (!(LMIC.opmode & OP_TXRXPEND)) 
  {
    double temperature;
    double humidity;

    readSensor(&temperature, &humidity);    
    
    lpp.reset();
    lpp.addTemperature(1, temperature);
    lpp.addRelativeHumidity(2, humidity);
    
    Serial.printf("Temperature : %.2f, Humidity : %.2f\r\n", temperature, humidity);

    // Prepare upstream data transmission at the next possible time.
    LMIC_setTxData2(1, lpp.getBuffer(), lpp.getSize(), 0);
         
    Serial.printf("Packet queued\r\n");
  }
  // Next TX is scheduled after TX_COMPLETE event.
}

void setup() 
{
  Serial.begin(115200);
  Serial.printf("Starting...\r\n");

   //Initialize I2C Communication
  Wire.begin();
  
  //Configure HDC1080
  Wire.beginTransmission(0x40);
  Wire.write(0x02);
  Wire.write(0x10);
  Wire.write(0x00);
  Wire.endTransmission();

  
  // LMIC init
  os_init();
  // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();
  // Set static session parameters. Instead of dynamically establishing a session
  // by joining the network, precomputed session parameters are be provided.
  uint8_t appskey[sizeof(APPSKEY)];
  uint8_t nwkskey[sizeof(NWKSKEY)];
  memcpy_P(appskey, APPSKEY, sizeof(APPSKEY));
  memcpy_P(nwkskey, NWKSKEY, sizeof(NWKSKEY));
  LMIC_setSession (0x1, DEVADDR, nwkskey, appskey);
  // Select frequencies range
  LMIC_selectSubBand(0);

  // Disable link check validation
  LMIC_setLinkCheckMode(0);
  // TTN uses SF9 for its RX2 window.
  LMIC.dn2Dr = DR_SF9;

// FOR TESTING ONLY!
for(int i=1; i<9; i++) { // For EU or AS923; for US use i<71
    LMIC_disableChannel(i);
}  
  // Set data rate and transmit power for uplink (note: txpow seems to be ignored by the library)
  LMIC_setDrTxpow(DR_SF7,14);
  Serial.printf("LMIC setup done!\r\n");
  // Start job
  do_send(&sendjob);
}

void loop() 
{    
  // Make sure LMIC is ran too
  os_runloop_once();
}
