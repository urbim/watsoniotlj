/*******************************************************************************
 * Copyright (c) 2014, 2015 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Sam Danbury - initial implementation
 *    Ian Craggs - refactoring to remove STL and other changes
 *    Sam Grove  - added check for Ethernet cable.
 *    Chris Styles - Added additional menu screen for software revision
 *    James Sutton - Mac fix and extra debug
 *    Ian Craggs - add not authorized messages
 *
 * To do :
 *    Add magnetometer sensor output to IoT data stream
 *
 *******************************************************************************/

#include "LM75B.h"
#include "MMA7660.h"
#include "MQTTClient.h"
#include "MQTTEthernet.h"
#include "C12832.h"
#include "Arial12x12.h"
#include "rtos.h"

// Update this to the next number *before* a commit
#define __APP_SW_REVISION__ "18"

// Configuration values needed to connect to IBM IoT Cloud
#define ORG "z6hsac"             // For a registered connection, replace with your organisation id
#define ID "mbedgw-01"                        // For a registered connection, replace with your device id
#define AUTH_TOKEN "firefly1"                // For a registered connection, replace with your device auth-token
#define TYPE "mbedGW" 


#define MQTT_PORT 1883
#define MQTT_TLS_PORT 8883
#define IBM_IOT_PORT MQTT_PORT

#define MQTT_MAX_PACKET_SIZE 250

#if defined(TARGET_UBLOX_C027)
#warning "Compiling for mbed C027"
#include "C027.h"
#elif defined(TARGET_LPC1768)
#warning "Compiling for mbed LPC1768"
#include "LPC1768.h"
#elif defined(TARGET_K64F)
#warning "Compiling for mbed K64F"
#include "K64F.h"
#endif


bool quickstartMode = true;
char org[11] = ORG;  
char type[30] = TYPE;
char id[30] = ID;                 // mac without colons
char auth_token[30] = AUTH_TOKEN; // Auth_token is only used in non-quickstart mode

bool connected = false;
bool mqttConnecting = false;
bool netConnected = false;
bool netConnecting = false;
bool ethernetInitialising = true;
int connack_rc = 0; // MQTT connack return code
int retryAttempt = 0;
int menuItem = 0;

//CUSTOM
int fireflyDelay = 1000; //ms
int fireflyDelayOld = 1000; //ms
int razlikaPot = 75; //*1000 vrednost na potenciometru
//END

char* joystickPos = "CENTRE";
int blink_interval = 0;

char* ip_addr = "";
char* gateway_addr = "";
char* host_addr = "";
int connectTimeout = 1000;

// If we wanted to manually set the MAC address,
// this is how to do it. In this example, we take
// the original Mbed Set MAC address and combine it
// with a prefix of our choosing.
 /*
extern "C" void $Super$$mbed_mac_address(char *s);
extern "C" void $Sub$$mbed_mac_address(char *s) 
{
    char originalMAC[6] = "";
    $Super$$mbed_mac_address(originalMAC);
    
    char mac[6];
    mac[0] = 0x00;
    mac[1] = 0x08;
    mac[2] = 0xdc;
    mac[3] = originalMAC[3];
    mac[4] = originalMAC[4];
    mac[5] = originalMAC[5];
    memcpy(s, mac, 6);
}
*/


void off()
{
    r = g = b = 1.0;    // 1 is off, 0 is full brightness
}

void red()
{
    r = 0.7; g = 1.0; b = 1.0;    // 1 is off, 0 is full brightness
}

void yellow()
{
    r = 0.7; g = 0.7; b = 1.0;    // 1 is off, 0 is full brightness
}

void green()
{
    r = 1.0; g = 0.7; b = 1.0;    // 1 is off, 0 is full brightness
}


void flashing_yellow(void const *args)
{
    bool on = false;
    while (!connected && connack_rc != MQTT_NOT_AUTHORIZED && connack_rc != MQTT_BAD_USERNAME_OR_PASSWORD)    // flashing yellow only while connecting 
    {
        on = !on; 
        if (on)
            yellow();
        else
            off();   
        wait(0.5);
    }
}


void flashing_red(void const *args)  // to be used when the connection is lost
{
    bool on = false;
    while (!connected)
    {
        on = !on;
        if (on)
            red();
        else
            off();
        wait(2.0);
    }
}


//CUSTOM
//za nastavit kateri parameter se spreminja na pot2 
//hue, sat
int sat = 1;
//END
void printMenu(int menuItem) 
{
    static char last_line1[30] = "", last_line2[30] = "";
    char line1[30] = "", line2[30] = "";
        
    switch (menuItem)
    {
        case 0:
            sprintf(line1, "IBM IoT Cloud CUSTOM");
            sprintf(line2, "Scroll with joystick");
            break;
        case 1:
            sprintf(line1, "Go to:");
            sprintf(line2, "http://ibm.biz/iotqstart");
            break;
        case 2:
            sprintf(line1, "Device Identity:");
            sprintf(line2, "%s", id);
            break;
        case 3:
            sprintf(line1, "MQTT Status:");
            if (mqttConnecting)
                sprintf(line2, "Connecting... %d/5", retryAttempt);
            else
            {
                if (connected)
                    sprintf(line2, "Connected");
                else
                {
                    switch (connack_rc)
                    {
                        case MQTT_CLIENTID_REJECTED:
                            sprintf(line2, "Clientid rejected");
                            break;
                        case MQTT_BAD_USERNAME_OR_PASSWORD:
                            sprintf(line2, "Invalid username or password");
                            break;
                        case MQTT_NOT_AUTHORIZED:
                            sprintf(line2, "Not authorized");
                            break;
                        default:
                            sprintf(line2, "Disconnected");
                    }
                }
            }
            break;
        case 4:
            sprintf(line1, "Ethernet State:");
            sprintf(line2, ethernetInitialising ? "Initializing..." : "Initialized");
            break;
        case 5:
            sprintf(line1, "Socket State:");
            if (netConnecting)
                sprintf(line2, "Connecting... %d/5", retryAttempt);
            else
                sprintf(line2, netConnected ? "Connected" : "Disconnected");
            break;
        case 6:
            sprintf(line1, "IP Address:");
            sprintf(line2, "%s", ip_addr);
            break;
        case 7:
            sprintf(line1, "Gateway:");
            sprintf(line2, "%s", gateway_addr);
            break;
        case 8:
            sprintf(line1, "App version:");
            sprintf(line2, "%s", __APP_SW_REVISION__);
            break;
        case 9:
            sprintf(line1, "Current Timeout:");
            sprintf(line2, "%d ms", connectTimeout);
            break;
        /*case 10:
            sprintf(line1, "Set FireFly freq");
            if (fireflyDelayOld != fireflyDelay) {
                sprintf(line2, "%d ms *", fireflyDelay);
            } else {
                sprintf(line2, "%d ms", fireflyDelay);    
            }       
            break;*/
        case 10:
            sprintf(line1, "Set pot2 LED param");
            if (sat == 1) {
                sprintf(line2, "Saturation");
            } else {
                sprintf(line2, "Hue");
            }
            break;
        case 11:
            sprintf(line1, "Set pot1,2 sensitivitiy");
            sprintf(line2, "%d", razlikaPot);
            break;
    }
    
    if (strcmp(line1, last_line1) != 0 || strcmp(line2, last_line2) != 0)
    {
        lcd.cls(); 
        lcd.locate(0, 0);
        lcd.printf(line1);
        strncpy(last_line1, line1, sizeof(last_line1));

        lcd.locate(0,16);
        lcd.printf(line2);
        strncpy(last_line2, line2, sizeof(last_line2));
    }
}

bool joystickLock = false;

void setMenu()
{
    
    //CUSTOM
    /*if (menuItem == 10) {
        if (Right && !joystickLock) {
            fireflyDelay += 100;
            joystickLock = true;
            printMenu(menuItem);
        } else if (Left && !joystickLock) {
            fireflyDelay -= 100;
            joystickLock = true;
            printMenu(menuItem);    
        } else if (Click) {
            fireflyDelayOld = fireflyDelay;
            //TODO: DEJANSKO NASTAVI FIREFLY DELAY   
            printMenu(menuItem);
        } else {
            joystickLock = false;
        }
    }*/
    if (menuItem == 10) {
        if ((Right || Left) && !joystickLock) {
            if (sat == 1) sat = 0;
            else sat = 1;
            joystickLock = true;
            printMenu(menuItem);
        } else {
            joystickLock = false;
        }  
    }
    if (menuItem == 11) {
        if (Right && !joystickLock) {
            razlikaPot += 5;
            joystickLock = true;
            printMenu(menuItem);
        } else if (Left && !joystickLock) {
            razlikaPot -= 5;
            joystickLock = true;
            printMenu(menuItem);    
        } else {
            joystickLock = false;
        }
    }
    //END
    
    if (Down)
    {
        joystickPos = "DOWN";
        if (menuItem >= 0 && menuItem < 11)
            printMenu(++menuItem);
    } 
    else if (Left)
        joystickPos = "LEFT";
    else if (Click)
        joystickPos = "CLICK";
    else if (Up)
    {
        joystickPos = "UP";
        if (menuItem <= 11 && menuItem > 0)
            printMenu(--menuItem);
    }
    else if (Right)
        joystickPos = "RIGHT";
    else
        joystickPos = "CENTRE";
}

void menu_loop(void const *args)
{
    int count = 0;
    while(true)
    {
        setMenu();
        if (++count % 10 == 0)
            printMenu(menuItem);
        Thread::wait(100);
    }
}


/**
 * Display a message on the LCD screen prefixed with IBM IoT Cloud
 */
void displayMessage(char* message)
{
    lcd.cls();
    lcd.locate(0,0);        
    lcd.printf("IBM IoT Cloud");
    lcd.locate(0,16);
    lcd.printf(message);
}


int connect(MQTT::Client<MQTTEthernet, Countdown, MQTT_MAX_PACKET_SIZE>* client, MQTTEthernet* ipstack)
{   
    const char* iot_ibm = ".messaging.internetofthings.ibmcloud.com";
    
    char hostname[strlen(org) + strlen(iot_ibm) + 1];
    sprintf(hostname, "%s%s", org, iot_ibm);
    EthernetInterface& eth = ipstack->getEth();
    ip_addr = eth.getIPAddress();
    gateway_addr = eth.getGateway();
    
    // Construct clientId - d:org:type:id
    char clientId[strlen(org) + strlen(type) + strlen(id) + 5];
    sprintf(clientId, "d:%s:%s:%s", org, type, id);
    
    // Network debug statements 
    LOG("=====================================\n");
    LOG("Connecting Ethernet.\n");
    LOG("IP ADDRESS: %s\n", eth.getIPAddress());
    LOG("MAC ADDRESS: %s\n", eth.getMACAddress());
    LOG("Gateway: %s\n", eth.getGateway());
    LOG("Network Mask: %s\n", eth.getNetworkMask());
    LOG("Server Hostname: %s\n", hostname);
    LOG("Client ID: %s\n", clientId);
    LOG("=====================================\n");
    
    netConnecting = true;
    int rc = ipstack->connect(hostname, IBM_IOT_PORT, connectTimeout);
    if (rc != 0)
    {
        WARN("IP Stack connect returned: %d\n", rc);    
        return rc;
    }
    netConnected = true;
    netConnecting = false;

    // MQTT Connect
    mqttConnecting = true;
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 3;
    data.clientID.cstring = clientId;
    
    if (!quickstartMode) 
    {        
        data.username.cstring = "use-token-auth";
        data.password.cstring = auth_token;
    }
    
    if ((rc = client->connect(data)) == 0) 
    {       
        connected = true;
        green();    
        displayMessage("Connected");
        wait(1);
        displayMessage("Scroll with joystick");
    }
    else
        WARN("MQTT connect returned %d\n", rc);
    if (rc >= 0)
        connack_rc = rc;
    mqttConnecting = false;
    return rc;
}


int getConnTimeout(int attemptNumber)
{  // First 10 attempts try within 3 seconds, next 10 attempts retry after every 1 minute
   // after 20 attempts, retry every 10 minutes
    return (attemptNumber < 10) ? 3 : (attemptNumber < 20) ? 60 : 600;
}


void attemptConnect(MQTT::Client<MQTTEthernet, Countdown, MQTT_MAX_PACKET_SIZE>* client, MQTTEthernet* ipstack)
{
    connected = false;
   
    // make sure a cable is connected before starting to connect
    while (!linkStatus()) 
    {
        wait(1.0f);
        WARN("Ethernet link not present. Check cable connection\n");
    }
        
    while (connect(client, ipstack) != MQTT_CONNECTION_ACCEPTED) 
    {    
        if (connack_rc == MQTT_NOT_AUTHORIZED || connack_rc == MQTT_BAD_USERNAME_OR_PASSWORD)
            return; // don't reattempt to connect if credentials are wrong
            
        Thread red_thread(flashing_red);

        int timeout = getConnTimeout(++retryAttempt);
        WARN("Retry attempt number %d waiting %d\n", retryAttempt, timeout);
        
        // if ipstack and client were on the heap we could deconstruct and goto a label where they are constructed
        //  or maybe just add the proper members to do this disconnect and call attemptConnect(...)
        
        // this works - reset the system when the retry count gets to a threshold
        if (retryAttempt == 5)
            NVIC_SystemReset();
        else
            wait(timeout);
    }
}

//CUSTOM
//ce ni bilo velike spremembe na potencimetru, ga ne upotevaj
int oldPot1 = 0;
int newPot1;
int oldPot2 = 0;
int newPot2;
//END

int publish(MQTT::Client<MQTTEthernet, Countdown, MQTT_MAX_PACKET_SIZE>* client, MQTTEthernet* ipstack)
{
    MQTT::Message message;
    char* pubTopic = "iot-2/evt/status/fmt/json";
            
            
    newPot1 = ain1.read() * 10000;
    newPot2 = ain2.read() * 10000;
    
    int r1 = newPot1-oldPot1;
    int r2 = newPot2-oldPot2;
    
    
    if (r1<0) r1*=-1;
    if (r2<0) r2*=-1;
    
    int refresh;
    if (r1 > razlikaPot || r2 > razlikaPot) {
        refresh = 1;
        oldPot1 = newPot1;
        oldPot2 = newPot2;
    } else {
        refresh = 0;
        return 0;
    }


    char buf[250];
    sprintf(buf,
     "{\"d\":{\"myName\":\"IoT mbed\",\"accelX\":%0.4f,\"accelY\":%0.4f,\"accelZ\":%0.4f,\"temp\":%0.4f,\"joystick\":\"%s\",\"potentiometer1\":%0.4f,\"potentiometer2\":%0.4f,\"refresh\":%d,\"pot2param_sat\":%d}}",
            MMA.x(), MMA.y(), MMA.z(), sensor.temp(), joystickPos, ain1.read(), ain2.read(), refresh, sat);
    message.qos = MQTT::QOS0;
    message.retained = false;
    message.dup = false;
    message.payload = (void*)buf;
    message.payloadlen = strlen(buf);
    
    LOG("Publishing %s\n", buf);
    return client->publish(pubTopic, message);
}


char* getMac(EthernetInterface& eth, char* buf, int buflen)    // Obtain MAC address
{   
    strncpy(buf, eth.getMACAddress(), buflen);

    char* pos;                                                 // Remove colons from mac address
    while ((pos = strchr(buf, ':')) != NULL)
        memmove(pos, pos + 1, strlen(pos) + 1);
    return buf;
}


void messageArrived(MQTT::MessageData& md)
{
    MQTT::Message &message = md.message;
    char topic[md.topicName.lenstring.len + 1];
    
    sprintf(topic, "%.*s", md.topicName.lenstring.len, md.topicName.lenstring.data);
    
    LOG("Message arrived on topic %s: %.*s\n",  topic, message.payloadlen, message.payload);
          
    // Command topic: iot-2/cmd/blink/fmt/json - cmd is the string between cmd/ and /fmt/
    char* start = strstr(topic, "/cmd/") + 5;
    int len = strstr(topic, "/fmt/") - start;
    
    if (memcmp(start, "blink", len) == 0)
    {
        char payload[message.payloadlen + 1];
        sprintf(payload, "%.*s", message.payloadlen, (char*)message.payload);
    
        char* pos = strchr(payload, '}');
        if (pos != NULL)
        {
            *pos = '\0';
            if ((pos = strchr(payload, ':')) != NULL)
            {
                int blink_rate = atoi(pos + 1);       
                blink_interval = (blink_rate <= 0) ? 0 : (blink_rate > 50 ? 1 : 50/blink_rate);
            }
        }
    }
    else
        WARN("Unsupported command: %.*s\n", len, start);
}


int main()
{    
    quickstartMode = (strcmp(org, "quickstart") == 0);

    lcd.set_font((unsigned char*) Arial12x12);  // Set a nice font for the LCD screen
    
    led2 = LED2_OFF; // K64F: turn off the main board LED 
    
    displayMessage("Connecting");
    Thread yellow_thread(flashing_yellow);
    Thread menu_thread(menu_loop);  
    
    LOG("***** IBM IoT Client Ethernet Example *****\n");
    MQTTEthernet ipstack;
    ethernetInitialising = false;
    MQTT::Client<MQTTEthernet, Countdown, MQTT_MAX_PACKET_SIZE> client(ipstack);
    LOG("Ethernet Initialized\n"); 
    
    if (quickstartMode)
        getMac(ipstack.getEth(), id, sizeof(id));
        
    attemptConnect(&client, &ipstack);
    
    if (connack_rc == MQTT_NOT_AUTHORIZED || connack_rc == MQTT_BAD_USERNAME_OR_PASSWORD)    
    {
        red();
        while (true)
            wait(1.0); // Permanent failures - don't retry
    }
    
    if (!quickstartMode) 
    {
        int rc = 0;
        if ((rc = client.subscribe("iot-2/cmd/+/fmt/json", MQTT::QOS1, messageArrived)) != 0)
            WARN("rc from MQTT subscribe is %d\n", rc); 
    }
    
    blink_interval = 0;
    int count = 0;
    while (true)
    {
        if (++count == 25)
        {               // Publish a message every second
            if (publish(&client, &ipstack) != 0) 
                attemptConnect(&client, &ipstack);   // if we have lost the connection
            count = 0;
        }
        
        if (blink_interval == 0)
            led2 = LED2_OFF;
        else if (count % blink_interval == 0)
            led2 = !led2;
        client.yield(10);  // allow the MQTT client to receive messages
    }
}
