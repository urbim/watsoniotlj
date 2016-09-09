import re, sys
import pexpect
import paho.mqtt.client as mqtt
import threading
import time

class myThread(threading.Thread):

    def __init__(self, threadID):
        print("Thread initialized!")
        threading.Thread.__init__(self)
        self.threadID = threadID

    def run(self):
        print("Thread started!")
        readAdvertise()
        print("Thread ended!")

#Callback for when the client receives a CONNACK response from the server.
def on_connect(client, userdata, flags, rc):
    print("Connected with result code " + str(rc))

    #Subscribing in on_connect() means tha if we lose the connection and reconnect the subscription will be renewed.
    #client.subscribe("firefly/command")

#The callback for then a PUBLISH message is received from the server.
def on_message(client, userdata, msg):
    print(msg.topic + " " +str(msg.payload))

def publishMQTT(data, name, clientMQTT):

    lux = (int(data[4:6],16) << 8) | int(data[6:8],16)

    t = (int(data[8:10],16) << 8) | int(data[10:12],16)
    temp = ((175.72 * t) / 65536) - 46.85

    rh = (int(data[0:2],16) << 8) | int(data[2:4],16)
    humid = ((125.0 * rh) / 65536) - 6


    if(name != "FF-160"): # FF-160 je senzor z acc
        sendData = ("{\"d\": {\"ID\":\"%s\", \"Lux\": %d, \"Temp\": %.1f,\"RelHum\" :%.1f}}" % (name, lux, temp, humid))
        #clientMQTT.publish("testing/all", sendData)
        clientMQTT.publish("iot-2/evt/status/fmt/json", sendData)
        print(sendData)

def measureDistance(txPower, rssi):
  if rssi == 0:
    return -1.0 # if we cannot determine accuracy, return -1.
  ratio = rssi * 1.0 / txPower
  if ratio < 1.0:
    return pow(ratio,10)
  else:
    return (0.89976) * pow(ratio, 7.7095) + 0.111

def readAdvertise():

    scan = pexpect.spawn("sudo hcitool lescan --duplicates 1")
    p = pexpect.spawn("sudo hcidump --raw")
    packet = ""

    while True:
        try:
          line = p.readline()
          packet = line[2:].strip()
          packet = packet.replace(" ", "")
          if(packet[32:38] == '46462D'):
            if packet[26:28] == '1E':
              line = p.readline()
              line += p.readline()
              packet += line.replace(" ", "")
              packet = packet.rstrip()
              name = packet[32:44].decode('hex')
              data = packet[58:72]
              publishMQTT(data, name, client)
        except:
            packet = ""
        packet = ""

time.sleep(30)

client = mqtt.Client("d:org-id:raspberrypi:SmartPie")
#client = mqtt.Client("d:quickstart:arduino:fftesting")
client.on_connect = on_connect
client.on_message = on_message

client.username_pw_set("use-token-auth", "auth-token")
#client.connect("quickstart.messaging.internetofthings.ibmcloud.com", 1883, 60)
client.connect("org-id.messaging.internetofthings.ibmcloud.com", 1883, 15)

t = myThread(1)
t.start()
client.loop_forever()
