import datetime

import paho.mqtt.publish as publish 
import paho.mqtt.client as mqtt

def fetch_mqtt_msg(mqtt_auth, mqtt_server, publish_topic, listen_topic):

    userdata = {'topic': listen_topic, 'response': False}
    timeout = 20

    client = mqtt.Client(userdata=userdata)
    client.on_connect = on_connect
    client.on_message = on_message
    client.username_pw_set(mqtt_auth['username'], mqtt_auth['password'])
    client.connect(mqtt_server, 1883, 60)

    publish_mqtt_msg(mqtt_auth, mqtt_server, publish_topic)

    now = datetime.datetime.now()

    while ((not userdata['response']) and ((datetime.datetime.now() - now).seconds < timeout)):
        client.loop()

    if (datetime.datetime.now() - now).seconds > timeout:
        userdata['response'] = None

    return userdata['response']

def publish_mqtt_msg(mqtt_auth, mqtt_server, publish_topic):
    publish.single(publish_topic, "publish", hostname=mqtt_server, auth=mqtt_auth)


def on_connect(client, userdata, flags, rc):
    client.subscribe(userdata['topic'])

def on_message(client, userdata, msg):
    global response
    if msg.topic == userdata['topic']:
            meas = float(msg.payload.decode("utf-8"))             
            userdata['response'] = meas
    else:
        userdata['response'] = 'Wrong topic: ' + msg.topic
