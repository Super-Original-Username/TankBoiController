/* Imports. Standard stuff. */
#include <bluefruit.h>
#include <Wire.h>
#include <Adafruit_MotorShield.h>


/* Declarations for motors and BLE services. */
BLEDis bledis;
BLEUart bleuart;
BLEBas blebas;
Adafruit_MotorShield AFMS = Adafruit_MotorShield();
Adafruit_DCMotor *l_motor = AFMS.getMotor(4);
Adafruit_DCMotor *r_motor = AFMS.getMotor(3);

/* Movement stuff. */
uint8_t dir;
const int BASE_SPEED = 150;
int curr_speed;
int turn_speed;
bool flip_controls;

/* These are used while reading packets from the android app. */
const int READ_BUFSIZE = 20;
uint8_t packetbuffer[READ_BUFSIZE+1];



void setup(){
  Serial.begin(115200);
  Bluefruit.autoConnLed(true);
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);

  Bluefruit.begin();
  Bluefruit.setTxPower(4); // Sets max power for the antenna
  Bluefruit.setName("TankBoi");
  Bluefruit.setConnectCallback(connect_callback);
  Bluefruit.setDisconnectCallback(disconnect_callback);

  bledis.setManufacturer("Adafruit Industries");
  bledis.setModel("Bluefruit Feather52");
  bledis.begin();
  Serial.println("Device initializing");
  
  curr_speed = BASE_SPEED;
  flip_controls = false;

  bleuart.begin(); // Starts the BLE UART service

  blebas.begin(); // Starts the battery service for reading in charge % from the main battery
  blebas.write(100);
  AFMS.begin();

  startAdv(); // Starts BLE advertising

  /* Initializes the motors. */
  r_motor->setSpeed(curr_speed);
  l_motor->setSpeed(curr_speed);
  r_motor->run(FORWARD);
  l_motor->run(FORWARD);
  r_motor->run(RELEASE);
  l_motor->run(RELEASE);
}

/* This starts the BLE advertising. */
void startAdv(void)
{

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();

  Bluefruit.Advertising.addService(bleuart);

  Bluefruit.ScanResponse.addName();

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
} 

/* This is where the fun begins. Movement calls are handled here for smooth vehicle control */
void loop(){
  int len = readPacket(&bleuart, 500); // Reads in from the BLE chip.
  blebas.write(analogRead(A7));
  if (len==0) return; // If no data is found, the loop begins anew
  turn_speed = curr_speed/2;
  if (packetbuffer[1] == 'B') {
    char b_num = packetbuffer[2];
    boolean pressed = packetbuffer[3]-'0';
    Serial.print ("Button "); Serial.print(b_num);
    if (pressed) {
      Serial.println(" pressed");
      switch(b_num){ // Determines action to perform based on input received
        case '1': // Decrements speed
          if(curr_speed > 75){ // With the motors we were using, anything less than 75 would stall them
            curr_speed -= 10;
            Serial.println(curr_speed);
          }
          break;
        case '2': // Speed increment. Demo day issues indicate that 250 is too high a speed, as it caused the motor board to need a reset
        if(curr_speed < 250){
            curr_speed += 10;
            Serial.println(curr_speed);
          }
          break;
        case '3': // This resets the vehicle's speed
          curr_speed = BASE_SPEED;
          Serial.println(curr_speed);
          break;
        case '4':
          flip_controls = !flip_controls; // This variable allows us to reverse the controls of the vehicle to make some obstacles easier to maneuver
          break;
        case '5': // Forward movement
          mv_fw(flip_controls);
          break;
        case '6': // Backward movement
          mv_fw(!flip_controls);
          break;
        case '7': // This function name is misleading. When called, this makes the vehicle turn right with zero radius
          dime_left(flip_controls);
          break;
        case '8': // Left turn with zero radius
          dime_left(!flip_controls);
          break;
        case '9': // This function makes the vehicle turn left with only the right tracks spinning. Useful for tricky terrain
          hard_left(flip_controls);
          break;
        case '0': // Makes the vehicle turn right by only powering the motor on the left side
          hard_left(!flip_controls);
          break;
        default:
          break;
      }
    } else {
      Serial.println(" released");
      r_motor->run(RELEASE);
      l_motor->run(RELEASE);
      r_motor->setSpeed(curr_speed); // resets motor speed only after the command changes, so turn functions don't have weird speed issues
      l_motor->setSpeed(curr_speed);
    }
  }
  // Request CPU to enter low-power mode until an event/interrupt occurs
  waitForEvent();
}


uint8_t readPacket(BLEUart *blart, uint16_t timeout){
  uint16_t origtimeout = timeout, replyidx = 0;
  memset(packetbuffer, 0, READ_BUFSIZE); // Tosses whatever data the BLE chip has into memory for later use
  while (timeout--) { // Simple timeout: if no data, do not continue
    if (replyidx >= 20) break;
    if ((packetbuffer[1] == 'B'))
      break;

    while (blart->available()) { // This determines the length of the message for the function to return
      char c =  blart->read();
      if (c == '!') {
        replyidx = 0;
      }
      packetbuffer[replyidx] = c;
      replyidx++;
      timeout = origtimeout;
    }
    
    if (timeout == 0) break;
    delay(1);
  }
  packetbuffer[replyidx] = 0;
  if(!replyidx)
    return 0;
  if(packetbuffer[0]!= '!')
    return 0;

  return replyidx;
}



/* The connect and disconnect callbacks were copied from Adafruit's example code. I didn't want anything to break here. */
void connect_callback(uint16_t conn_handle)
{
  char central_name[32] = { 0 };
  Bluefruit.Gap.getPeerName(conn_handle, central_name, sizeof(central_name));
  Serial.print("Connected to ");
  Serial.println(central_name);
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason)
{
  (void) conn_handle;
  (void) reason;
  Serial.println();
  Serial.println("Disconnected");
}

/* 
Below are all of the movement functions. I tried to be clever by using just one function for each movement pair 
(forward/backward, dime left/dime right, etc.), but that made the dime turning a bit wonky. Everything else worked as intended.
*/

void mv_fw(bool flip){ // Powers both motors in the same direction (poor wording, I know. I guess it really spins them in opposing directions). The 'flip' boolean determines direction
  if(!flip){
    r_motor->run(FORWARD);
    l_motor->run(FORWARD);
  }else{
    r_motor->run(BACKWARD);
    l_motor->run(BACKWARD);
  }
}

void dime_left(bool flip){ // Turn with a zero radius. Uses the 'flip' boolean to determine which motor to spin
  r_motor->setSpeed(turn_speed);
  l_motor->setSpeed(turn_speed);
  if(!flip){
    r_motor->run(FORWARD);
    l_motor->run(BACKWARD);
  }else{
    l_motor->run(FORWARD);
    r_motor->run(BACKWARD);
  }
}

void hard_left(bool right){ // Powers only one motor for better maneuverability on rough terrain. Uses the 'right' boolean to determine which motor to power
  r_motor->setSpeed(turn_speed);
  l_motor->setSpeed(turn_speed);
  if(right){
    r_motor->run(FORWARD);
  }else{
    l_motor->run(FORWARD);
  }
}