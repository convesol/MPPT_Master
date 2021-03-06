//----------------------------------------------------------------------------------------------------
//  ARDUINO MPPT SOLAR CHARGE CONTROLLER (Version-3) 
//  Author: Debasish Dutta/deba168
//          www.opengreenenergy.com
//
//  This code is for an arduino Nano based Solar MPPT charge controller.
//  This code is a modified version of sample code from www.timnolan.com
//  dated 08/02/2015
//
//  Mods by Aplavins 01/07/2015
//
//  Size batteries and panels appropriately. Larger panels need larger batteries and vice versa.

////  Specifications :  //////////////////////////////////////////////////////////////////////////////////////////////////////
                                                                                                                            //
//    1.Solar panel power = 50W                                            
                                                                                                                            //
//    2.Rated Battery Voltage= 12V ( lead acid type )

//    3.Maximum current = 5A                                                                                                //

//    4.Maximum load current =10A                                                                                            //

//    5. In put Voltage = Solar panel with Open circuit voltage from 17 to 25V                                               //

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




#include "TimerOne.h"               // using Timer1 library from http://www.arduino.cc/playground/Code/Timer1
#include <LiquidCrystal_I2C.h>      // using the LCD I2C Library from https://bitbucket.org/fmalpartida/new-liquidcrystal/downloads
#include <Wire.h>  

//----------------------------------------------------------------------------------------------------------
 
//////// Arduino pins Connections//////////////////////////////////////////////////////////////////////////////////

// A0 - Voltage divider (solar)
// A1 - ACS 712 Out
// A2 - Voltage divider (battery)
// A4 - LCD SDA
// A5 - LCD SCL
// D2 - ESP8266 Tx
// D3 - ESP8266 Rx through the voltage divider
// D5 - LCD back control button
// D6 - Load Control 
// D8 - 2104 MOSFET driver SD
// D9 - 2104 MOSFET driver IN  
// D11- Green LED
// D12- Yellow LED
// D13- Red LED

// Full scheatic is given at http://www.instructables.com/files/orig/F9A/LLR8/IAPASVA1/F9ALLR8IAPASVA1.pdf

///////// Definitions /////////////////////////////////////////////////////////////////////////////////////////////////


#define SOL_VOLTS_CHAN 0                   // defining the adc channel to read solar volts
#define SOL_AMPS_CHAN 1                    // Defining the adc channel to read solar amps
#define BAT_VOLTS_CHAN 2                   // defining the adc channel to read battery volts
#define AVG_NUM 8                          // number of iterations of the adc routine to average the adc readings

// ACS 712 Current Sensor is used. Current Measured = (5/(1024 *0.185))*ADC - (2.5/0.185) 

#define SOL_AMPS_SCALE  0.026393581        // the scaling value for raw adc reading to get solar amps   // 5/(1024*0.185)
#define SOL_VOLTS_SCALE 0.029296875        // the scaling value for raw adc reading to get solar volts  // (5/1024)*(R1+R2)/R2 // R1=100k and R2=20k
#define BAT_VOLTS_SCALE 0.029296875        // the scaling value for raw adc reading to get battery volts 

#define PWM_PIN 9                          // the output pin for the pwm (only pin 9 avaliable for timer 1 at 50kHz)
#define PWM_ENABLE_PIN 8                   // pin used to control shutoff function of the IR2104 MOSFET driver (hight the mosfet driver is on)

#define TURN_ON_MOSFETS digitalWrite(PWM_ENABLE_PIN, HIGH)      // enable MOSFET driver
#define TURN_OFF_MOSFETS digitalWrite(PWM_ENABLE_PIN, LOW)      // disable MOSFET driver

#define ONE_SECOND 50000                   // count for number of interrupt in 1 second on interrupt period of 20us

#define MAX_BAT_VOLTS 15.0                 // we don't want the battery going any higher than this
#define BAT_FLOAT 13.6                     // battery voltage we want to stop charging at
#define LVD 11.5                           // Low voltage disconnect
#define OFF_NUM 9                          // number of iterations of off charger state

  
//------------------------------------------------------------------------------------------------------
//Defining led pins for indication
#define LED_RED 11
#define LED_GREEN 12
#define LED_YELLOW 13
//-----------------------------------------------------------------------------------------------------
// Defining load control pin
#define LOAD_PIN 6       // pin-2 is used to control the load
  
//-----------------------------------------------------------------------------------------------------
// Defining lcd back light pin
#define BACK_LIGHT_PIN 5       // pin-5 is used to control the lcd back light
//------------------------------------------------------------------------------------------------------
/////////////////////////////////////////BIT MAP ARRAY//////////////////////////////////////////////////
//-------------------------------------------------------------------------------------------------------
byte solar[8] = //icon for termometer
{
  0b11111,
  0b10101,
  0b11111,
  0b10101,
  0b11111,
  0b10101,
  0b11111,
  0b00000
};

byte battery[8]=
{
  0b01110,
  0b11011,
  0b10001,
  0b10001,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
};

byte _PWM [8]=
{
  0b11101,
  0b10101,
  0b10101,
  0b10101,
  0b10101,
  0b10101,
  0b10101,
  0b10111,
};
//-------------------------------------------------------------------------------------------------------

// global variables

float sol_amps;                       // solar amps 
float sol_volts;                      // solar volts 
float bat_volts;                      // battery volts 
float sol_watts;                      // solar watts
float old_sol_watts = 0;              // solar watts from previous time through ppt routine
float loaded_volts = 0;               // variable for storing battery voltage under load
float deltaV = 0;                     // vairable for storing the difference of voltage when under load
unsigned int seconds = 0;             // seconds from timer routine
unsigned int prev_seconds = 0;        // seconds value from previous pass
unsigned int interrupt_counter = 0;   // counter for 20us interrrupt
unsigned long time = 0;               // variable to store time the back light control button was pressed in millis
int pulseWidth = 512;                 // pwm duty cycle 0-1024
int pwm = 0;                          // mapped value of pulseWidth in %
int back_light_pin_State = 0;         // variable for storing the state of the backlight button
int load_status = 0;                  // variable for storing the load output state (for writing to LCD)
int prev_load_status = 0;             // to see when the load has been turned on
int trackDirection = 1;               // step amount to change the value of pulseWidth used by MPPT algorithm
  
enum charger_mode {no_battery, sleep, bulk, Float, error} charger_state;  // enumerated variable that holds state for charger state machine
// set the LCD address to 0x27 for a 20 chars 4 line display
// Set the pins on the I2C chip used for LCD connections:
//                    addr, en,rw,rs,d4,d5,d6,d7,bl,blpol
LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);  // Set the LCD I2C address


//------------------------------------------------------------------------------------------------------
// This routine is automatically called at powerup/reset
//------------------------------------------------------------------------------------------------------

void setup()                           // run once, when the sketch starts
{
  pinMode(LED_RED, OUTPUT);            // sets the digital pin as output
  pinMode(LED_GREEN, OUTPUT);          // sets the digital pin as output
  pinMode(LED_YELLOW, OUTPUT);         // sets the digital pin as output
  pinMode(PWM_ENABLE_PIN, OUTPUT);     // sets the digital pin as output
  TURN_OFF_MOSFETS;                    // turn off MOSFET driver chip
  Timer1.initialize(20);               // initialize timer1, and set a 20uS period
  Timer1.attachInterrupt(callback);    // attaches callback() as a timer overflow interrupt
  Serial.begin(9600);                  // open the serial port at 38400 bps:
  charger_state = sleep;               // start with charger state as sleep
  pinMode(BACK_LIGHT_PIN, INPUT);      // backlight on button
  pinMode(LOAD_PIN,OUTPUT);            // output for the LOAD MOSFET (LOW = on, HIGH = off)
  digitalWrite(LOAD_PIN,HIGH);         // default load state is OFF
  lcd.begin(20,4);                     // initialize the lcd for 16 chars 2 lines, turn on backlight
  lcd.noBacklight();                   // turn off the backlight
  lcd.createChar(1,solar);             // turn the bitmap into a character
  lcd.createChar(2,battery);           // turn the bitmap into a character
  lcd.createChar(3,_PWM);              // turn the bitmap into a character
}

//------------------------------------------------------------------------------------------------------
// Main loop
//------------------------------------------------------------------------------------------------------
void loop()                         
{
  read_data();                         // read data from inputs
  mode_select();                       // select the charging state
  set_charger();                       // run the charger state machine
  print_data();                        // print data
  load_control();                      // control the connected load
  led_output();                        // led indication
  lcd_display();                       // lcd display
  
}


//------------------------------------------------------------------------------------------------------
// This routine reads and averages the analog inputs for this system, solar volts, solar amps and 
// battery volts. 
//------------------------------------------------------------------------------------------------------
int read_adc(int channel){
  
  int sum = 0;
  int temp;
  int i;
  
  for (i=0; i<AVG_NUM; i++) {          // loop through reading raw adc values AVG_NUM number of times  
    temp = analogRead(channel);        // read the input pin  
    sum += temp;                       // store sum for averaging
    delayMicroseconds(50);             // pauses for 50 microseconds  
  }
  return(sum / AVG_NUM);               // divide sum by AVG_NUM to get average and return it
}

//------------------------------------------------------------------------------------------------------
// This routine reads all the analog input values for the system. Then it multiplies them by the scale
// factor to get actual value in volts or amps. 
//------------------------------------------------------------------------------------------------------
void read_data(void) {
  old_sol_watts = sol_watts;                                       // save the previous value of sol watts
  sol_amps = (read_adc(SOL_AMPS_CHAN) * SOL_AMPS_SCALE -13.51);    // input of solar amps
  sol_volts = read_adc(SOL_VOLTS_CHAN) * SOL_VOLTS_SCALE;          // input of solar volts 
  bat_volts = read_adc(BAT_VOLTS_CHAN) * BAT_VOLTS_SCALE;          // input of battery volts 
  sol_watts = sol_amps * sol_volts ;                               // calculations of solar watts                  
}

//------------------------------------------------------------------------------------------------------
// This is interrupt service routine for Timer1 that occurs every 20uS.
//
//------------------------------------------------------------------------------------------------------
void callback()
{
  if (interrupt_counter++ > ONE_SECOND) {        // increment interrupt_counter until one second has passed
    interrupt_counter = 0;                       // reset the counter
    seconds++;                                   // then increment seconds counter
  }
}

void mode_select(){
  if (bat_volts < 10.0) charger_state = no_battery ;                                           // If battery voltage is below 10, there is no battery connected or dead / wrong battery
  else if (bat_volts > MAX_BAT_VOLTS) charger_state = error;                                   // If battery voltage is over 15, there's a problem
  else if ((bat_volts > 10.0) && (bat_volts < MAX_BAT_VOLTS) && (sol_volts > MAX_BAT_VOLTS)){  // If battery voltage is in the normal range and there is light on the panel
    if (bat_volts >= (BAT_FLOAT-0.1)) charger_state = Float;                                   // If battery voltage is above 13.5, go into float charging
    else charger_state = bulk;                                                                 // If battery voltage is less than 13.5, go into bulk charging
  }
  else if (sol_volts < MAX_BAT_VOLTS){                                                         // If there's no light on the panel, go to sleep
    charger_state = sleep;
  }
}

void set_charger(void) {
    
  switch (charger_state){                                                                     // skip to the state that is currently set
    
    case no_battery:                                                                          // the charger is in the no battery state
      disable_charger();                                                                      // Disable the MOSFET driver
      break;
    
    case sleep:                                                                               // the charger is in the sleep state
      pulseWidth = 512;                                                                       // set the duty cycle to 50% for when it comes on again
      disable_charger();                                                                      // Disable the MOSFET driver
      break;
      
    case bulk:                                                                                // the charger is in the bulk state
      PerturbAndObserve();                                                                    // run the MPPT algorithm
      enable_charger();                                                                       // If battery voltage is below 13.6 enable the MOSFET driver
      break;
      
    case Float:                                                                               // the charger is in the float state, it uses PWM instead of MPPT
      pulseWidth = 1022;                                                                      // set the duty cycle to maximum (we don't need MPPT here the battery is charged)
      if (bat_volts < BAT_FLOAT) enable_charger();                                            // If battery voltage is below 13.6 enable the MOSFET driver
      else disable_charger();                                                                 // If above, disable the MOSFET driver
      break;
      
    case error:                                                                               // if there's something wrong
      disable_charger();                                                                      // Disable the MOSFET driver
      break;                                                                                  
      
    default:                                                                                  // if none of the other cases are satisfied,
      disable_charger();                                                                      // Disable the MOSFET driver
      break;
  }
}

void PerturbAndObserve(){
  if ((pulseWidth == 300) || (pulseWidth == 1022) || (sol_watts < old_sol_watts)) trackDirection = -trackDirection;  // if pulseWidth has hit one of the ends reverse the track direction
  pulseWidth = pulseWidth + trackDirection;                                                                          // add (or subtract) track Direction to(from) pulseWidth
}

void enable_charger() {
  pulseWidth = constrain (pulseWidth, 300, 1022);               // prevent overflow of pulseWidth and not fully on or off for the charge pump
  pwm = map(pulseWidth, 0, 1024, 0, 100);                       // use pulseWidth to get a % value and store it in pwm
  Timer1.pwm(PWM_PIN, pulseWidth, 20);                          // use Timer1 routine to set pwm duty cycle at 20uS period
  TURN_ON_MOSFETS;						// enable the MOSFET driver																	
}

void disable_charger() {
  TURN_OFF_MOSFETS;                                             // disable MOSFET driver
}	

//----------------------------------------------------------------------------------------------------------------------
/////////////////////////////////////////////LOAD CONTROL/////////////////////////////////////////////////////
//----------------------------------------------------------------------------------------------------------------------  
  
void load_control(){
  if ((sol_watts < 1) && (bat_volts > (LVD - deltaV))){            // If the panel isn't producing, it's probably night
    load_status = 1;                                               // record that the load is on
  }
  else if (bat_volts < (LVD - deltaV)){                            // If the battery voltage drops below the low voltage threshold
    digitalWrite(LOAD_PIN, HIGH);                                  // turn the load off
    load_status = 0;                                               // record that the load is off
  }  
  else if (sol_watts > 1){                                         // If the panel is producing, it's day time
    digitalWrite(LOAD_PIN, HIGH);                                  // turn the load off
    load_status = 0;                                               // record that the load is off
  }
  if ((load_status = 1) && (prev_load_status = 0)){                // If the load status has changed from off to on
    digitalWrite(LOAD_PIN, LOW);                                   // turn the load on
    loaded_volts = read_adc(BAT_VOLTS_CHAN) * BAT_VOLTS_SCALE;     // input of battery volts under load
    deltaV = (bat_volts - loaded_volts);                           // record the difference in voltage
  }
  prev_load_status = load_status;                                  // save the load status
}

//------------------------------------------------------------------------------------------------------
// This routine prints all the data out to the serial port.
//------------------------------------------------------------------------------------------------------
void print_data(void) {
  
  Serial.print(seconds,DEC);
  Serial.print("      ");

 //no_battery, sleep, bulk, Float, error
  Serial.print("Charging = ");
  if (charger_state == no_battery) Serial.print("noBat");
  else if (charger_state == sleep) Serial.print("sleep");
  else if (charger_state == bulk) Serial.print("bulk");
  else if (charger_state == Float) Serial.print("float");
  else if (charger_state == error) Serial.print("error");
  Serial.print("      ");

  Serial.print("pwm = ");
  Serial.print(pwm,DEC);
  Serial.print("      ");

  Serial.print("Current (panel) = ");
  //print_int100_dec2(sol_amps);
  Serial.print(sol_amps);
  Serial.print("      ");

  Serial.print("Voltage (panel) = ");
  Serial.print(sol_volts);
  //print_int100_dec2(sol_volts);
  Serial.print("      ");

  Serial.print("Power (panel) = ");
  Serial.print(sol_volts);
  // print_int100_dec2(sol_watts);
  Serial.print("      ");

  Serial.print("Battery Voltage = ");
  Serial.print(bat_volts);
  //print_int100_dec2(bat_volts);
  Serial.print("      ");

  Serial.print("\n\r");
  //delay(1000);
}

//-------------------------------------------------------------------------------------------------
//---------------------------------Led Indication--------------------------------------------------
//-------------------------------------------------------------------------------------------------

void led_output(void)
{
  if(bat_volts > 14.1 )
  {
      leds_off_all();
      digitalWrite(LED_YELLOW, HIGH); 
  } 
  else if(bat_volts > 11.9 && bat_volts < 14.1)
  {
      leds_off_all();
      digitalWrite(LED_GREEN, HIGH);
  } 
  else if(bat_volts < 11.8)
  {
      leds_off_all;
      digitalWrite(LED_RED, HIGH); 
  } 
  
}

//------------------------------------------------------------------------------------------------------
//
// This function is used to turn all the leds off
//
//------------------------------------------------------------------------------------------------------
void leds_off_all(void)
{
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_YELLOW, LOW);
}

//------------------------------------------------------------------------------------------------------
//-------------------------- LCD DISPLAY --------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------
void lcd_display()
{
  back_light_pin_State = digitalRead(BACK_LIGHT_PIN);
  if (back_light_pin_State == HIGH)
  {
    time = millis();                        // If any of the buttons are pressed, save the time in millis to "time"
  }
 
 lcd.setCursor(0, 0);
 lcd.print("SOL");
 lcd.setCursor(4, 0);
 lcd.write(1);
 lcd.setCursor(0, 1);
 lcd.print(sol_volts);
 lcd.print("V"); 
 lcd.setCursor(0, 2);
 lcd.print(sol_amps);
 lcd.print("A");  
 lcd.setCursor(0, 3);
 lcd.print(sol_watts);
 lcd.print("W "); 
 lcd.setCursor(8, 0);
 lcd.print("BAT");
 lcd.setCursor(12, 0);
 lcd.write(2);
 lcd.setCursor(8, 1);
 lcd.print(bat_volts);
 lcd.setCursor(8,2);
 
 //no_battery, sleep, bulk, Float, error
 
 if (charger_state == no_battery) 
 lcd.print("no batt");
 else if (charger_state == sleep)
 lcd.print("sleep");
 else if (charger_state == bulk)
 lcd.print("bulk");
 else if (charger_state == Float)
 lcd.print("float");
 else if (charger_state == error)
 lcd.print("error");
 
 //-----------------------------------------------------------
 //--------------------Battery State Of Charge ---------------
 //-----------------------------------------------------------
 lcd.setCursor(8,3);
 if ( bat_volts >= 12.7)
 lcd.print( "100%");
 else if (bat_volts >= 12.5 && bat_volts < 12.7)
 lcd.print( "90%");
 else if (bat_volts >= 12.42 && bat_volts < 12.5)
 lcd.print( "80%");
 else if (bat_volts >= 12.32 && bat_volts < 12.42)
 lcd.print( "70%");
 else if (bat_volts >= 12.2 && bat_volts < 12.32)
 lcd.print( "60%");
 else if (bat_volts >= 12.06 && bat_volts < 12.2)
 lcd.print( "50%");
 else if (bat_volts >= 11.90 && bat_volts < 12.06)
 lcd.print( "40%");
 else if (bat_volts >= 11.75 && bat_volts < 11.90)
 lcd.print( "30%");
 else if (bat_volts >= 11.58 && bat_volts < 11.75)
 lcd.print( "20%");
 else if (bat_volts >= 11.31 && bat_volts < 11.58)
 lcd.print( "10%");
 else if (bat_volts < 11.3)
 lcd.print( "0%");
 
//--------------------------------------------------------------------- 
//------------------Duty Cycle-----------------------------------------
//---------------------------------------------------------------------
 lcd.setCursor(15,0);
 lcd.print("PWM");
 lcd.setCursor(19,0);
 lcd.write(3);
 lcd.setCursor(15,1);
 lcd.print(pwm); 
 lcd.print("%");
 //----------------------------------------------------------------------
 //------------------------Load Status-----------------------------------
 //----------------------------------------------------------------------
 lcd.setCursor(15,2);
 lcd.print("Load");
 lcd.setCursor(15,3);
 if (load_status == 1)
 {
    lcd.print("On");
 }
 else
 {
   lcd.print("Off");
 }
 backLight_timer();                      // call the backlight timer function in every loop 
}

void backLight_timer(){
  if((millis() - time) <= 15000)         // if it's been less than the 15 secs, turn the backlight on
      lcd.backlight();                   // finish with backlight on  
  else 
      lcd.noBacklight();                 // if it's been more than 15 secs, turn the backlight off
}
